/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-delayed-task-runner.h"

#include <algorithm>
#include <utility>

namespace fcitx {

    LotusDelayedTaskRunner::~LotusDelayedTaskRunner() {
        cancelPendingTasks();
    }

    void LotusDelayedTaskRunner::runAfter(int delayMs, std::function<void()> task) {
        if (!task) {
            return;
        }
        delayMs = std::max(delayMs, 0);
        {
            std::scoped_lock lock(workerMutex_);
            if (!worker_.joinable()) {
                workerStop_ = false;
                worker_     = std::thread(&LotusDelayedTaskRunner::workerLoop, this);
            }
            delayedTasks_.push_back({.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), .generation = taskGeneration_, .task = std::move(task)});
        }
        workerCv_.notify_one();
    }

    void LotusDelayedTaskRunner::cancelPendingTasks() {
        {
            std::scoped_lock lock(workerMutex_);
            ++taskGeneration_;
            delayedTasks_.clear();
            workerStop_ = true;
        }
        workerCv_.notify_all();
        if (worker_.joinable() && std::this_thread::get_id() != worker_.get_id()) {
            worker_.join();
        }
    }

    void LotusDelayedTaskRunner::workerLoop() {
        std::unique_lock<std::mutex> lock(workerMutex_);
        while (!workerStop_) {
            if (delayedTasks_.empty()) {
                workerCv_.wait(lock);
                continue;
            }
            auto next = std::ranges::min_element(delayedTasks_, [](const DelayedTask& a, const DelayedTask& b) { return a.due < b.due; });
            if (std::chrono::steady_clock::now() < next->due) {
                workerCv_.wait_until(lock, next->due);
                continue;
            }
            DelayedTask task = std::move(*next);
            delayedTasks_.erase(next);
            const bool current = task.generation == taskGeneration_;
            lock.unlock();
            if (current) {
                task.task();
            }
            lock.lock();
        }
    }

} // namespace fcitx
