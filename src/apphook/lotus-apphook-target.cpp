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
#include <cstdlib>
#include <new>
#include <cerrno>
#include <chrono>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace lotus::apphook {
    namespace {
        // An eventfd notification is complete once its 8-byte write succeeds,
        // or the counter is already nonzero/saturated (EAGAIN).
        void notifyFd(int fd) {
            if (fd < 0)
                return;
            const uint64_t one = 1;
            ssize_t        result;
            do {
                result = write(fd, &one, sizeof(one));
            } while (result < 0 && errno == EINTR);
            // Runtime shutdown/connection teardown handles permanent fd errors.
        }
    } // namespace

    void stopRuntimeWorker() {
        Runtime& state = runtime();
        // The child may inherit locked mutexes and a thread that no longer exists.
        // It must not join or tear down the parent's worker.
        if (state.ownerPid.load(std::memory_order_acquire) != static_cast<int>(getpid()))
            return;
        std::scoped_lock lock(state.workerMutex);
        state.initialized.store(false, std::memory_order_release);
        state.shuttingDown.store(true, std::memory_order_release);
        notifyFd(state.completionFd.load(std::memory_order_acquire));
        if (state.workerJoinable) {
            const int joinStatus = pthread_join(state.worker, nullptr);
            (void)joinStatus; // No worker recovery is possible during destruction.
            state.workerJoinable = false;
        }
        const int done = state.completionFd.exchange(-1, std::memory_order_acq_rel);
        if (done >= 0)
            close(done);
    }

    Runtime::Runtime() : ownerPid(static_cast<int>(getpid())) {}

    Runtime::~Runtime() = default;

    namespace {
        std::atomic<Runtime*> currentRuntime{nullptr};
    }

    Runtime& runtime() {
        auto* current = currentRuntime.load(std::memory_order_acquire);
        if (current)
            return *current;
        auto*    fresh    = new Runtime;
        Runtime* expected = nullptr;
        if (!currentRuntime.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) {
            delete fresh;
            return *expected;
        }
        return *fresh;
    }

    namespace {
        void  connectLoop();
        void* runWorker(void*) {
            connectLoop();
            return nullptr;
        }

        /** @brief Starts the workers on first focus or keyboard activity, outside library initialization. */
        void startWorkers() {
            Runtime&         state = runtime();
            std::scoped_lock workerLock(state.workerMutex);
            if (!state.initialized.load(std::memory_order_acquire) || state.shuttingDown.load(std::memory_order_acquire) ||
                state.workersStarted.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            const int completion = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (completion < 0) {
                state.workersStarted.store(false, std::memory_order_release);
                return;
            }
            state.completionFd.store(completion, std::memory_order_release);
            if (pthread_create(&state.worker, nullptr, &runWorker, nullptr) != 0) {
                state.completionFd.store(-1, std::memory_order_release);
                close(completion);
                state.workersStarted.store(false, std::memory_order_release);
                return;
            }
            state.workerJoinable = true;
        }

        /** @brief Signals the IPC worker when a keyboard focus state changes. */
        void signalChange() {
            const int fd = runtime().completionFd.load(std::memory_order_acquire);
            notifyFd(fd);
        }

        /** @brief Returns whether either application keyboard frontend owns focus. */
        bool hasKeyboardFocus() {
            Runtime& state = runtime();
            return state.focusedX11.load(std::memory_order_acquire) || state.focusedWayland.load(std::memory_order_acquire) || state.focusedXcb.load(std::memory_order_acquire);
        }
    } // namespace

    void ensureRuntimeInitialized() {
        static std::atomic<int> registeredPid{0};
        const int               pid = static_cast<int>(getpid());
        if (registeredPid.exchange(pid, std::memory_order_acq_rel) != pid)
            std::atexit(&stopRuntimeWorker);
        runtime().initialized.store(true, std::memory_order_release);
    }

    extern "C" __attribute__((visibility("hidden"))) int lotus_apphook_resume_child_runtime() {
        Runtime* fresh = new (std::nothrow) Runtime;
        if (!fresh)
            return 0;
        Runtime* inherited = currentRuntime.exchange(fresh, std::memory_order_acq_rel);
        if (inherited) {
            const int done = inherited->completionFd.load(std::memory_order_acquire);
            const int conn = inherited->connectionFd.load(std::memory_order_acquire);
            if (done >= 0)
                close(done);
            if (conn >= 0 && conn != done)
                close(conn);
        }
        resetXcbAfterFork();
        resetWaylandAfterFork();
        resetX11AfterFork();
        ensureRuntimeInitialized();
        return 1;
    }

    void noteX11Focus(bool focused) {
        if (runtime().focusedX11.exchange(focused, std::memory_order_acq_rel) != focused) {
            signalChange();
        }
        if (focused) {
            startWorkers();
        }
    }

    void noteX11KeyboardAvailable(bool available) {
        if (runtime().readyX11.exchange(available, std::memory_order_acq_rel) != available) {
            signalChange();
        }
        if (available)
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

    void noteXcbFocus(bool focused) {
        if (runtime().focusedXcb.exchange(focused, std::memory_order_acq_rel) != focused)
            signalChange();
        if (focused)
            startWorkers();
    }

    void noteXcbKeyboardAvailable(bool available) {
        if (runtime().readyXcb.exchange(available, std::memory_order_acq_rel) != available)
            signalChange();
        if (available)
            startWorkers();
    }

    void noteKeyboardActivity(bool wayland) {
        if (wayland) {
            noteWaylandFocus(true);
        } else {
            noteX11Focus(true);
        }
        startWorkers();
    }

    std::shared_ptr<Task> takeTask(HookBackend backend, const void* connection, uintptr_t focused) {
        Runtime&         state = runtime();
        std::scoped_lock lock(state.mutex);
        const auto       it = std::find_if(state.pending.begin(), state.pending.end(), [=](const auto& task) {
            const auto& target = task->target;
            return target.backend == backend && target.connection == connection && target.focused == focused;
        });
        if (it == state.pending.end())
            return {};
        auto task = std::move(*it);
        state.pending.erase(it);
        return task;
    }

    void cancelPendingTasks(HookBackend backend, const void* connection) {
        if (!connection)
            return;
        Runtime&         state = runtime();
        std::scoped_lock lock(state.mutex);
        for (auto it = state.pending.begin(); it != state.pending.end();) {
            const auto& target = (*it)->target;
            if (target.backend == backend && target.connection == connection) {
                (*it)->cancelled.store(true, std::memory_order_release);
                finishLocked(*it, -EINPROGRESS);
                it = state.pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    void finishLocked(const std::shared_ptr<Task>& task, int status) {
        if (task && !task->done) {
            task->status = status;
            task->done   = true;
            const int fd = runtime().completionFd.load(std::memory_order_acquire);
            notifyFd(fd);
        }
    }

    void wakeApplication() {
        // Xlib waiters have independent eventfds: another Display must never
        // consume a wake intended for the focused target.
        wakeX11();
        wakeWayland();
        wakeXcb();
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
            return state.readyX11.load() || state.readyWayland.load() || state.readyXcb.load();
        }

        /** Choose an unambiguous focused frontend without holding runtime.mutex.
         * Backend focus locks can be acquired before runtime.mutex by event hooks.
         */
        HookTarget selectTarget() {
            HookTarget selected{};
            unsigned   total = 0;
            for (const auto candidates : {focusedX11Candidates(), focusedXcbCandidates(), focusedWaylandCandidates()}) {
                total += candidates.count;
                if (candidates.count == 1)
                    selected = candidates.target;
            }
            return total == 1 ? selected : HookTarget{};
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
                if (!op.count || op.keycode == KEY_RESERVED || op.keycode > KEY_MAX || op.modifiers > 15 || taps > kMaxTaps) {
                    return -EINVAL;
                }
            }
            const HookTarget target = selectTarget();
            if (target.backend == HookBackend::None)
                return -EAGAIN; // No unique active keyboard frontend for this PID.
            auto task       = std::make_shared<Task>();
            task->target    = target;
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

        struct ConnectionRegistration {
            Runtime& state;
            explicit ConnectionRegistration(Runtime& state) : state(state) {}
            void set(int fd) {
                state.connectionFd.store(fd, std::memory_order_release);
            }
            ~ConnectionRegistration() {
                state.connectionFd.store(-1, std::memory_order_release);
            }
        };

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
                ConnectionRegistration registration(state);
                FdGuard                connection(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
                registration.set(connection.get());
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
                    pollfd    fds[3] = {{connection.get(), POLLIN, 0}, {state.completionFd.load(), POLLIN, 0}, {waylandWakeFlushFd(), POLLOUT, 0}};
                    const int result = poll(fds, 3, -1);
                    if (state.shuttingDown.load(std::memory_order_acquire))
                        break;
                    if (result < 0 && errno == EINTR) {
                        continue;
                    }
                    if (result < 0 || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                        break;
                    }
                    if (fds[2].revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL))
                        retryWaylandWakeFlush();
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
                        ssize_t  received;
                        do {
                            received = read(fds[1].fd, &drained, sizeof(drained));
                        } while (received < 0 && errno == EINTR);
                        // A competing notifier may have already drained the fd.
                        (void)received;
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

    } // namespace
} // namespace lotus::apphook

extern "C" __attribute__((visibility("hidden"))) void lotus_apphook_initialize_runtime() {
    lotus::apphook::ensureRuntimeInitialized();
}
