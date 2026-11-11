/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file lotus-delayed-task-runner.h
 * @brief Worker thread that runs a task after a delay, off the fcitx main thread.
 */

#ifndef _LOTUS_DELAYED_TASK_RUNNER_H_
#define _LOTUS_DELAYED_TASK_RUNNER_H_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "common-export.h"

namespace fcitx {

    /**
     * @brief Runs tasks after a delay on one shared worker thread.
     *
     * The keyboard backends use it for the replacement pre/post delay and for pacing injected keys, so the fcitx
     * main thread never sleeps. Cancelling invalidates every queued task through a generation counter.
     */
    class LOTUS_EXPORT LotusDelayedTaskRunner {
      public:
        LotusDelayedTaskRunner() = default;
        ~LotusDelayedTaskRunner();

        LotusDelayedTaskRunner(const LotusDelayedTaskRunner&)            = delete;
        LotusDelayedTaskRunner& operator=(const LotusDelayedTaskRunner&) = delete;
        LotusDelayedTaskRunner(LotusDelayedTaskRunner&&)                 = delete;
        LotusDelayedTaskRunner& operator=(LotusDelayedTaskRunner&&)      = delete;

        /**
         * @brief Queues a task to run after the delay on the worker thread.
         * @param delayMs Delay in milliseconds; a negative value runs as soon as possible.
         * @param task Task to run; an empty task is ignored.
         */
        void runAfter(int delayMs, std::function<void()> task);

        /**
         * @brief Drops queued tasks and joins the worker.
         */
        void cancelPendingTasks();

      private:
        struct DelayedTask {
            std::chrono::steady_clock::time_point due;
            uint64_t                              generation;
            std::function<void()>                 task;
        };

        void                     workerLoop();

        std::thread              worker_;
        std::mutex               workerMutex_;
        std::condition_variable  workerCv_;
        std::vector<DelayedTask> delayedTasks_;
        uint64_t                 taskGeneration_ = 0;
        bool                     workerStop_     = false;
    };

} // namespace fcitx

#endif
