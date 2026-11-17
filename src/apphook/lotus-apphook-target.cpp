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
#include "lotus-apphook-fd.h"
#include "common-fd-guard.h"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
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
#include <ctime>
#include <unordered_map>
#include <utility>
#include <unistd.h>

namespace lotus::apphook {
    namespace {
        // An eventfd notification is complete once its 8-byte write succeeds,
        // or the counter is already nonzero/saturated (EAGAIN).
        void notifyFd(int fd) {
            if (fd < 0)
                return;
            const uint64_t one = 1;
            // Runtime shutdown/connection teardown handles permanent fd errors.
            (void)retryOnEintr([&] { return write(fd, &one, sizeof(one)); });
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
        if (current != nullptr)
            return *current;
        auto     fresh    = std::make_unique<Runtime>();
        Runtime* expected = nullptr;
        if (!currentRuntime.compare_exchange_strong(expected, fresh.get(), std::memory_order_acq_rel))
            return *expected;
        // The runtime stays alive for the rest of the process.
        return *fresh.release();
    }

    namespace {
        void  connectLoop();
        void* runWorker(void* /*unused*/) {
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

        uint32_t focusStampNow() {
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            const auto stamp = static_cast<uint32_t>((static_cast<uint64_t>(now.tv_sec) * 1000U) + (static_cast<uint64_t>(now.tv_nsec) / 1000000U));
            return stamp == 0U ? 1U : stamp;
        }

        bool publishFocus(std::atomic<bool>& field, bool focused) {
            Runtime&         state = runtime();
            std::scoped_lock lock(state.focusStateMutex);
            if (field.exchange(focused, std::memory_order_acq_rel) == focused)
                return false;
            state.focusStamp = focusStampNow();
            return true;
        }

        struct FocusSnapshot {
            bool     focused = false;
            uint32_t stamp   = 0;
        };

        // Focus messages from separate processes arrive on separate sockets;
        // the event-time stamp lets the server ignore an older transition.
        FocusSnapshot keyboardFocusSnapshot() {
            Runtime&         state = runtime();
            std::scoped_lock lock(state.focusStateMutex);
            return {.focused = hasKeyboardFocus(), .stamp = state.focusStamp};
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
        std::unique_ptr<Runtime> fresh;
        try {
            fresh = std::make_unique<Runtime>();
        } catch (const std::bad_alloc&) { return 0; }
        // The inherited runtime belongs to the parent's threads; never free it here.
        Runtime* inherited = currentRuntime.exchange(fresh.release(), std::memory_order_acq_rel);
        if (inherited != nullptr) {
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
        if (publishFocus(runtime().focusedX11, focused))
            signalChange();
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
        if (publishFocus(runtime().focusedWayland, focused))
            signalChange();
        // Wayland publishes while holding its keyboard-state lock; worker
        // startup is performed separately after that lock is released.
    }

    void noteWaylandKeyboardAvailable(bool available) {
        if (runtime().readyWayland.exchange(available, std::memory_order_acq_rel) != available) {
            signalChange();
        }
    }

    void ensureWaylandWorkerStarted() {
        startWorkers();
    }

    void noteXcbFocus(bool focused) {
        if (publishFocus(runtime().focusedXcb, focused))
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
        if (!wayland)
            noteX11Focus(true);
        startWorkers();
    }

    std::shared_ptr<Task> takeTask(HookBackend backend, const void* connection, uintptr_t focused) {
        Runtime&         state = runtime();
        std::scoped_lock lock(state.mutex);
        const auto       it = std::ranges::find_if(state.pending, [=](const auto& task) {
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
        if (connection == nullptr)
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
                if (std::cmp_not_equal(received, kRequestHeaderSize)) {
                    return -EINVAL;
                }
                {
                    std::scoped_lock lock(state.mutex);
                    const auto       taskIt = tasks.find(request.requestId);
                    if (taskIt != tasks.end()) {
                        const auto& task = taskIt->second;
                        task->cancelled.store(true, std::memory_order_release);
                        const auto pendingIt = std::ranges::find(state.pending, task);
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
                std::cmp_not_equal(received, kRequestHeaderSize + (request.count * sizeof(KeyOperation)))) {
                return -EINVAL;
            }
            if (!backendReady()) {
                return -EAGAIN;
            }
            const auto used = std::span(request.ops).first(request.count);
            uint64_t   taps = 0;
            for (const auto& op : used) {
                taps += op.count;
                if ((op.count == 0U) || op.keycode == KEY_RESERVED || op.keycode > KEY_MAX || op.modifiers > 15 || taps > kMaxTaps) {
                    return -EINVAL;
                }
            }
            const HookTarget target = selectTarget();
            if (target.backend == HookBackend::None)
                return -EAGAIN; // No unique active keyboard frontend for this PID.
            auto task       = std::make_shared<Task>();
            task->target    = target;
            task->requestId = request.requestId;
            task->ops.assign(used.begin(), used.end());
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
                        finished.push_back({.requestId = it->first, .status = it->second->status, .applied = it->second->applied.load(std::memory_order_acquire)});
                        --state.inFlight;
                        it = tasks.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            return std::ranges::all_of(finished, [fd](const Completion& finishedTask) {
                return reply(fd, ResponseKind::Complete, finishedTask.requestId, finishedTask.status, finishedTask.applied ? kCompleteMayHaveApplied : 0);
            });
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
                const auto it = std::ranges::find(state.pending, task);
                if (it != state.pending.end()) {
                    state.pending.erase(it);
                }
                finishLocked(task, -ENOTCONN);
                --state.inFlight;
            }
            tasks.clear();
        }

        class ConnectionRegistration {
          public:
            explicit ConnectionRegistration(Runtime& state) : state_(&state) {}
            ConnectionRegistration(const ConnectionRegistration&)            = delete;
            ConnectionRegistration& operator=(const ConnectionRegistration&) = delete;
            ConnectionRegistration(ConnectionRegistration&&)                 = delete;
            ConnectionRegistration& operator=(ConnectionRegistration&&)      = delete;
            ~ConnectionRegistration() {
                state_->connectionFd.store(-1, std::memory_order_release);
            }
            void set(int fd) {
                state_->connectionFd.store(fd, std::memory_order_release);
            }

          private:
            Runtime* state_;
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
                const bool    reportedReady = true;
                FocusSnapshot reportedFocus = keyboardFocusSnapshot();
                if (!reply(connection.get(), ResponseKind::Ready, 0, 0, reportedFocus.focused ? kReadyFocused : 0)) {
                    continue;
                }
                if (reportedFocus.focused && !reply(connection.get(), ResponseKind::Focus, 0, 1, reportedFocus.stamp)) {
                    continue;
                }
                std::unordered_map<uint64_t, std::shared_ptr<Task>> tasks;
                while (!state.shuttingDown.load()) {
                    std::array fds{
                        pollfd{.fd = connection.get(), .events = POLLIN, .revents = 0},
                        pollfd{.fd = state.completionFd.load(), .events = POLLIN, .revents = 0},
                        pollfd{.fd = waylandWakeFlushFd(), .events = POLLOUT, .revents = 0},
                    };
                    const int result = poll(fds.data(), fds.size(), -1);
                    if (state.shuttingDown.load(std::memory_order_acquire))
                        break;
                    if (result < 0 && errno == EINTR) {
                        continue;
                    }
                    if (result < 0 || hasPollEvents(fds[0].revents, POLLERR | POLLHUP | POLLNVAL)) {
                        break;
                    }
                    if (hasPollEvents(fds[2].revents, POLLOUT | POLLERR | POLLHUP | POLLNVAL))
                        retryWaylandWakeFlush();
                    if (hasPollEvents(fds[0].revents, POLLIN)) {
                        Request       request{};
                        const ssize_t received = recv(connection.get(), &request, sizeof(request), MSG_TRUNC);
                        if (std::cmp_less(received, kRequestHeaderSize) || std::cmp_greater(received, sizeof(request)) || request.magic != kMagic || request.version != kVersion) {
                            break;
                        }
                        const int status = dispatch(request, received, tasks);
                        if (request.type == Command::Emit && status != 0 && !reply(connection.get(), ResponseKind::Complete, request.requestId, status)) {
                            break;
                        }
                    }
                    if (hasPollEvents(fds[1].revents, POLLIN)) {
                        uint64_t drained = 0;
                        // A competing notifier may have already drained the fd.
                        (void)retryOnEintr([&] { return read(fds[1].fd, &drained, sizeof(drained)); });
                        if (!flushCompleted(connection.get(), tasks)) {
                            break;
                        }
                        const bool          currentReady = backendReady();
                        const FocusSnapshot currentFocus = keyboardFocusSnapshot();
                        if (currentReady != reportedReady) {
                            // A lost backend invalidates the session; reconnect after recovery.
                            break;
                        }
                        if (currentFocus.focused != reportedFocus.focused) {
                            reportedFocus = currentFocus;
                            if (!reply(connection.get(), ResponseKind::Focus, 0, reportedFocus.focused ? 1 : 0, reportedFocus.stamp)) {
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
