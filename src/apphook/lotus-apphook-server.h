/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-server.h
 * @brief Declares the Fcitx5-side abstract socket server and AppHook registry.
 */

#ifndef _SRC_LOTUS_APPHOOK_SERVER_H_
#define _SRC_LOTUS_APPHOOK_SERVER_H_

#include "lotus-apphook-protocol.h"
#include "common-fd-guard.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sys/types.h>

namespace fcitx {

    /** @brief Accepts AppHook clients and dispatches keyboard events by authenticated PID. */
    class LotusAppHook final {
      public:
        using Completion = std::function<void(int)>;

        /** @brief Starts the shared IPC listener and request worker. */
        LotusAppHook();
        /** @brief Stops the IPC listener, disconnects clients and releases pending work. */
        ~LotusAppHook();

        LotusAppHook(const LotusAppHook&)            = delete;
        LotusAppHook& operator=(const LotusAppHook&) = delete;
        LotusAppHook(LotusAppHook&&)                 = delete;
        LotusAppHook& operator=(LotusAppHook&&)      = delete;

        /**
         * @brief Checks whether a registered target has an active keyboard hook.
         * @param pid Focused application's PID.
         * @return True if the authenticated connection reported readiness.
         */
        [[nodiscard]] bool ready(pid_t pid) const;
        /** @brief Returns the authenticated PID whose client most recently reported keyboard focus. */
        [[nodiscard]] pid_t focusedPid() const;
        /**
         * @brief Queues a keyboard batch for a connected target.
         * @param pid Target application PID.
         * @param operations Ordered key operations.
         * @param requestId Unique request identifier.
         * @param preDelayMs Delay before dispatch, in milliseconds.
         * @param postDelayMs Delay before completion callback, in milliseconds.
         * @param complete Callback receiving zero or a negative error code.
         * @return True if the request was queued.
         */
        bool emit(pid_t pid, std::vector<lotus::apphook::KeyOperation> operations, uint64_t requestId, uint32_t preDelayMs, uint32_t postDelayMs, Completion complete);
        /** @brief Returns an identifier for a new keyboard batch. */
        uint64_t nextRequestId() {
            return nextRequest_.fetch_add(1, std::memory_order_relaxed);
        }
        /**
         * @brief Cancels a queued or target-side keyboard request.
         * @param pid Target application PID.
         * @param requestId Identifier to cancel.
         * @return Zero if accepted, otherwise a negative error code.
         */
        int cancel(pid_t pid, uint64_t requestId);
        /** @brief Stops the listener and request worker. */
        void shutdown();

      private:
        struct Session {
            explicit Session(int peerFd, pid_t peerPid) : fd(peerFd), pid(peerPid) {}

            FdGuard                 fd;
            pid_t                   pid;
            std::atomic<bool>       connected{true};
            std::atomic<bool>       ready{false};
            std::atomic<bool>       focused{false};
            std::mutex              sendMutex;
            std::mutex              mutex;
            std::condition_variable completed;
            uint64_t                waitingId = 0;
            bool                    hasReply  = false;
            int                     result    = 0;
        };

        struct Work {
            pid_t                                     pid;
            std::vector<lotus::apphook::KeyOperation> operations;
            uint64_t                                  requestId;
            uint32_t                                  preDelayMs;
            uint32_t                                  postDelayMs;
            Completion                                completion;
        };

        /** @brief Listens for client registration and completion messages. */
        void serve();
        /** @brief Processes queued keyboard requests outside the Fcitx5 main thread. */
        void run();
        /**
         * @brief Sends one message to a session without allowing concurrent writes.
         * @param session Target session.
         * @param request Wire message.
         * @param size Serialized size.
         * @return Zero or negative errno.
         */
        static int sendRequest(const std::shared_ptr<Session>& session, const lotus::apphook::Request& request, size_t size);
        /**
         * @brief Sends a batch and waits for the matching completion.
         * @param work Queued keyboard request.
         * @return Target completion status or transport error.
         */
        int sendKeys(const Work& work);
        /**
         * @brief Marks a session disconnected and wakes its pending request.
         * @param session Session to close.
         */
        static void disconnect(const std::shared_ptr<Session>& session);

        mutable std::mutex                                   mutex_;
        std::condition_variable                              condition_;
        std::deque<Work>                                     queue_;
        std::unordered_map<pid_t, std::shared_ptr<Session>>  sessions_;
        pid_t                                                focusedPid_ = 0;
        std::unordered_set<uint64_t>                         cancelled_;
        uint64_t                                             activeRequestId_ = 0;
        std::atomic<uint64_t>                                nextRequest_{1};
        std::atomic<bool>                                    stopping_{false};
        std::thread                                          server_;
        std::thread                                          worker_;
    };

} // namespace fcitx

#endif // _SRC_LOTUS_APPHOOK_SERVER_H_
