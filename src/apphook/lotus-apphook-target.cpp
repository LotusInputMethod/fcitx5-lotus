/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-target.cpp
 * @brief Connects the application-local keyboard hook to the Fcitx5 IPC server.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-target.h"
#include "common-fd-guard.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <link.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace lotus::apphook {

    Runtime& runtime() {
        // Injected libraries remain resident for the lifetime of the application.
        static Runtime state;
        return state;
    }

    namespace {
        void connectLoop();
        void scanForHooks();

        /** @brief Starts the workers on first focus or keyboard activity, outside library initialization. */
        void startWorkers() {
            Runtime& state = runtime();
            if (!state.initialized.load(std::memory_order_acquire) || state.workersStarted.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            const int wake       = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            const int completion = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (wake < 0 || completion < 0) {
                if (wake >= 0)
                    close(wake);
                if (completion >= 0)
                    close(completion);
                state.workersStarted.store(false, std::memory_order_release);
                return;
            }
            state.wakeFd.store(wake, std::memory_order_release);
            state.completionFd.store(completion, std::memory_order_release);
            try {
                std::thread(connectLoop).detach();
            } catch (const std::system_error&) {
                state.wakeFd.store(-1, std::memory_order_release);
                state.completionFd.store(-1, std::memory_order_release);
                close(wake);
                close(completion);
                state.workersStarted.store(false, std::memory_order_release);
                return;
            }
            if (state.lateAttached.load(std::memory_order_acquire)) {
                try {
                    std::thread(scanForHooks).detach();
                } catch (const std::system_error&) {
                    // IPC remains active even if the optional retroactive scanner fails.
                }
            }
        }

        /** @brief Signals the IPC worker when a keyboard focus state changes. */
        void signalChange() {
            const int fd = runtime().completionFd.load(std::memory_order_acquire);
            if (fd >= 0) {
                const uint64_t one = 1;
                (void)write(fd, &one, sizeof(one));
            }
        }

        /** @brief Returns whether either application keyboard frontend owns focus. */
        bool hasKeyboardFocus() {
            Runtime& state = runtime();
            return state.focusedX11.load(std::memory_order_acquire) || state.focusedWayland.load(std::memory_order_acquire);
        }
    } // namespace

    void noteX11Focus(bool focused) {
        if (runtime().focusedX11.exchange(focused, std::memory_order_acq_rel) != focused) {
            signalChange();
        }
        if (focused) {
            startWorkers();
        }
    }

    void noteX11KeyboardAvailable() {
        if (!runtime().readyX11.exchange(true, std::memory_order_acq_rel)) {
            signalChange();
        }
        startWorkers();
    }

    void noteWaylandFocus(bool focused) {
        if (runtime().focusedWayland.exchange(focused, std::memory_order_acq_rel) != focused) {
            signalChange();
        }
        if (focused) {
            startWorkers();
        }
    }

    void noteWaylandKeyboardAvailable(bool available) {
        if (runtime().readyWayland.exchange(available, std::memory_order_acq_rel) != available) {
            signalChange();
        }
        if (available) {
            startWorkers();
        }
    }

    void noteKeyboardActivity(bool wayland) {
        if (wayland) {
            noteWaylandFocus(true);
        } else {
            noteX11Focus(true);
        }
        startWorkers();
    }

    std::shared_ptr<Task> takeTask() {
        Runtime&         state = runtime();
        std::scoped_lock lock(state.mutex);
        if (state.pending.empty()) {
            return {};
        }
        auto task = std::move(state.pending.front());
        state.pending.pop_front();
        return task;
    }

    void finishLocked(const std::shared_ptr<Task>& task, int status) {
        if (task && !task->done) {
            task->status = status;
            task->done   = true;
            const int fd = runtime().completionFd.load(std::memory_order_acquire);
            if (fd >= 0) {
                const uint64_t one = 1;
                (void)write(fd, &one, sizeof(one));
            }
        }
    }

    void wakeApplication() {
        const int fd = runtime().wakeFd.load(std::memory_order_acquire);
        if (fd >= 0) {
            const uint64_t one = 1;
            (void)write(fd, &one, sizeof(one));
        }
        wakeWayland();
    }

    namespace {
        /**
         * @brief Sends a readiness change or request completion to the addon.
         * @param fd Connected IPC socket.
         * @param type Message kind.
         * @param requestId Request identifier, zero for readiness.
         * @param status Zero or negative errno-style status.
         * @param flags Initial focus flags for Ready, zero for other messages.
         * @return True when the message was sent.
         */
        bool reply(int fd, ResponseKind type, uint64_t requestId, int status, uint32_t flags = 0) {
            const Response response{.type = type, .status = status, .reserved = flags, .requestId = requestId};
            return send(fd, &response, sizeof(response), MSG_NOSIGNAL | MSG_DONTWAIT) == sizeof(response);
        }

        /** @brief Reports whether a supported application keyboard backend is installed. */
        bool backendReady() {
            Runtime& state = runtime();
            return state.readyX11.load() || state.readyWayland.load();
        }

        /**
         * @brief Validates and enqueues one message received from the Fcitx5 addon.
         * @param request Wire request payload.
         * @param received Received packet length.
         * @param tasks Outstanding requests on this connection.
         * @return Zero when the request was accepted; otherwise negative errno.
         */
        int dispatch(const Request& request, ssize_t received, std::unordered_map<uint64_t, std::shared_ptr<Task>>& tasks) {
            Runtime& state = runtime();
            if (request.type == Command::Cancel) {
                if (received != static_cast<ssize_t>(kRequestHeaderSize)) {
                    return -EINVAL;
                }
                {
                    std::scoped_lock lock(state.mutex);
                    const auto       taskIt = tasks.find(request.requestId);
                    if (taskIt != tasks.end()) {
                        const auto& task = taskIt->second;
                        task->cancelled.store(true, std::memory_order_release);
                        const auto pendingIt = std::find(state.pending.begin(), state.pending.end(), task);
                        if (pendingIt != state.pending.end()) {
                            state.pending.erase(pendingIt);
                            finishLocked(task, -ECANCELED);
                        }
                    }
                }
                wakeApplication();
                return 0;
            }
            if (request.type != Command::Emit || request.requestId == 0 || request.count == 0 || request.count > kMaxOps ||
                received != static_cast<ssize_t>(kRequestHeaderSize + request.count * sizeof(KeyOperation))) {
                return -EINVAL;
            }
            if (!backendReady()) {
                return -EAGAIN;
            }
            uint64_t taps = 0;
            for (size_t i = 0; i < request.count; ++i) {
                const auto& op = request.ops[i];
                taps += op.count;
                if (!op.count || op.modifiers > 15 || taps > kMaxTaps) {
                    return -EINVAL;
                }
            }
            auto task       = std::make_shared<Task>();
            task->requestId = request.requestId;
            task->ops.assign(request.ops.begin(), request.ops.begin() + request.count);
            {
                std::scoped_lock lock(state.mutex);
                if (state.inFlight >= kMaxTasks || tasks.contains(request.requestId)) {
                    return -EAGAIN;
                }
                ++state.inFlight;
                state.pending.push_back(task);
                tasks.emplace(request.requestId, std::move(task));
            }
            wakeApplication();
            return 0;
        }

        /**
         * @brief Acknowledges completed keyboard tasks on a persistent connection.
         * @param fd Connected IPC socket.
         * @param tasks Outstanding tasks on this connection.
         * @return False when writing a completion failed.
         */
        bool flushCompleted(int fd, std::unordered_map<uint64_t, std::shared_ptr<Task>>& tasks) {
            Runtime& state = runtime();
            struct Completion {
                uint64_t requestId;
                int      status;
                bool     applied;
            };
            std::vector<Completion> finished;
            {
                std::scoped_lock lock(state.mutex);
                for (auto it = tasks.begin(); it != tasks.end();) {
                    if (it->second->done) {
                        finished.push_back({it->first, it->second->status, it->second->applied.load(std::memory_order_acquire)});
                        --state.inFlight;
                        it = tasks.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            for (const auto& finishedTask : finished) {
                if (!reply(fd, ResponseKind::Complete, finishedTask.requestId, finishedTask.status, finishedTask.applied ? kCompleteMayHaveApplied : 0)) {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Releases pending keyboard work after the server disconnects.
         * @param tasks Outstanding tasks owned by this connection.
         */
        void discard(std::unordered_map<uint64_t, std::shared_ptr<Task>>& tasks) {
            Runtime&         state = runtime();
            std::scoped_lock lock(state.mutex);
            for (const auto& [requestId, task] : tasks) {
                task->cancelled.store(true, std::memory_order_release);
                const auto it = std::find(state.pending.begin(), state.pending.end(), task);
                if (it != state.pending.end()) {
                    state.pending.erase(it);
                }
                finishLocked(task, -ENOTCONN);
                --state.inFlight;
            }
            tasks.clear();
        }

        /** @brief Reconnects to the shared Fcitx5 listener and handles its requests. */
        void connectLoop() {
            using namespace std::chrono_literals;
            Runtime&    state = runtime();
            sockaddr_un address{};
            socklen_t   addressSize = 0;
            if (!socketAddress(address, addressSize)) {
                return;
            }
            while (!state.shuttingDown.load()) {
                // Do not register an idle or unsupported keyboard frontend.
                if (!backendReady()) {
                    std::this_thread::sleep_for(250ms);
                    continue;
                }
                FdGuard connection(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
                if (connection.get() < 0 || connect(connection.get(), reinterpret_cast<sockaddr*>(&address), addressSize) != 0) {
                    std::this_thread::sleep_for(250ms);
                    continue;
                }
                ucred     peer{};
                socklen_t length = sizeof(peer);
                if (getsockopt(connection.get(), SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 || length != sizeof(peer) || peer.uid != getuid()) {
                    std::this_thread::sleep_for(250ms);
                    continue;
                }
                if (!backendReady()) {
                    continue;
                }
                const bool reportedReady   = true;
                bool       reportedFocused = hasKeyboardFocus();
                if (!reply(connection.get(), ResponseKind::Ready, 0, 0, reportedFocused ? kReadyFocused : 0)) {
                    continue;
                }
                std::unordered_map<uint64_t, std::shared_ptr<Task>> tasks;
                while (!state.shuttingDown.load()) {
                    pollfd    fds[2] = {{connection.get(), POLLIN, 0}, {state.completionFd.load(), POLLIN, 0}};
                    const int result = poll(fds, 2, -1);
                    if (result < 0 && errno == EINTR) {
                        continue;
                    }
                    if (result < 0 || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                        break;
                    }
                    if (fds[0].revents & POLLIN) {
                        Request       request{};
                        const ssize_t received = recv(connection.get(), &request, sizeof(request), MSG_TRUNC);
                        if (received < static_cast<ssize_t>(kRequestHeaderSize) || received > static_cast<ssize_t>(sizeof(request)) || request.magic != kMagic ||
                            request.version != kVersion) {
                            break;
                        }
                        const int status = dispatch(request, received, tasks);
                        if (request.type == Command::Emit && status != 0 && !reply(connection.get(), ResponseKind::Complete, request.requestId, status)) {
                            break;
                        }
                    }
                    if (fds[1].revents & POLLIN) {
                        uint64_t drained;
                        (void)read(fds[1].fd, &drained, sizeof(drained));
                        if (!flushCompleted(connection.get(), tasks)) {
                            break;
                        }
                        const bool currentReady   = backendReady();
                        const bool currentFocused = hasKeyboardFocus();
                        if (currentReady != reportedReady) {
                            // A lost backend invalidates the session; reconnect after recovery.
                            break;
                        }
                        if (currentFocused != reportedFocused) {
                            reportedFocused = currentFocused;
                            if (!reply(connection.get(), ResponseKind::Focus, 0, reportedFocused ? 1 : 0)) {
                                break;
                            }
                        }
                    }
                }
                discard(tasks);
                std::this_thread::sleep_for(250ms);
            }
        }

        /** @brief Installs hooks for keyboard libraries already loaded in this process. */
        void installHookPass() {
            loadX11();
            loadWayland();
            const int x11     = installX11Hooks();
            const int wayland = installWaylandHooks();
            if (std::getenv("LOTUS_HOOK_DEBUG") && (x11 || wayland)) {
                std::fprintf(stderr, "LH hooks patched x=%d w=%d\n", x11, wayland);
            }
        }

        /** @brief Continues scanning for libraries loaded after focus or keyboard activity. */
        void scanForHooks() {
            using namespace std::chrono_literals;
            Runtime& state     = runtime();
            bool     lastReady = backendReady();
            while (!state.shuttingDown.load()) {
                installHookPass();
                const bool currentReady = backendReady();
                if (currentReady != lastReady) {
                    lastReady = currentReady;
                    signalChange();
                }
                std::this_thread::sleep_for(250ms);
            }
        }

        /** @brief Distinguishes an executable preload from a library loaded after process startup. */
        bool isLateLoad() {
            struct LinkOrder {
                bool seenLibc  = false;
                bool foundSelf = false;
                bool late      = false;
            } order;
            dl_iterate_phdr(
                [](dl_phdr_info* info, size_t, void* context) {
                    auto&       order = *static_cast<LinkOrder*>(context);
                    const char* name  = info->dlpi_name;
                    if (!name)
                        return 0;
                    if (std::strstr(name, "liblotus-apphook.so")) {
                        order.foundSelf = true;
                        order.late      = order.seenLibc;
                        return 1;
                    }
                    const char* base = std::strrchr(name, '/');
                    base             = base ? base + 1 : name;
                    if (std::strcmp(base, "libc.so.6") == 0)
                        order.seenLibc = true;
                    return 0;
                },
                &order);
            return !order.foundSelf || order.late;
        }

        /** @brief Keeps global preloads dormant; installs relocation hooks for late-loaded copies. */
        __attribute__((constructor)) void initialize() {
            Runtime&   state = runtime();
            const bool late  = isLateLoad();
            state.lateAttached.store(late, std::memory_order_release);
            if (late)
                installHookPass();
            state.initialized.store(true, std::memory_order_release);
        }

    } // namespace
} // namespace lotus::apphook
