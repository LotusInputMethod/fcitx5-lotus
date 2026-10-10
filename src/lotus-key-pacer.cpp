/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-key-pacer.h"
#include "lotus-utils.h"

#include <algorithm>
#include <linux/input-event-codes.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>

namespace fcitx {

KeyJob buildBackspaceJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post) {
    KeyJob job;
    if (count == 0)
        return job;
    job.reserve((count * 2) + 1);
    job.push_back(SleepFor{pre});
    for (size_t i = 0; i < count; ++i) {
        if (i > 0)
            job.push_back(SleepFor{(i + 1 == count) ? std::max(interval, post) : interval});
        job.push_back(KeyMsg{KEY_BACKSPACE, 1});
        job.push_back(KeyMsg{KEY_BACKSPACE, 0});
    }
    return job;
}

KeyJob buildSelectJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post) {
    KeyJob job;
    if (count == 0)
        return job;
    job.reserve((count * 3) + 7);
    job.push_back(SleepFor{pre});
    job.push_back(KeyMsg{KEY_LEFTSHIFT, 1});
    for (size_t i = 0; i < count; ++i) {
        job.push_back(SleepFor{interval});
        job.push_back(KeyMsg{KEY_LEFT, 1});
        job.push_back(KeyMsg{KEY_LEFT, 0});
    }
    job.push_back(SleepFor{std::chrono::milliseconds{10}});
    job.push_back(KeyMsg{KEY_LEFTSHIFT, 0});
    job.push_back(KeyMsg{KEY_DELETE, 1});
    job.push_back(KeyMsg{KEY_DELETE, 0});
    job.push_back(SleepFor{post});
    return job;
}

KeyPacer& KeyPacer::instance() {
    static KeyPacer pacer;
    return pacer;
}

KeyPacer::KeyPacer()  = default;
KeyPacer::~KeyPacer() = default;

void KeyPacer::init(Instance* instance) {
    instance_ = instance;
    kb_.init(&instance->eventLoop());
    kb_.setResumeCallback([this] { pump(); });
}

bool KeyPacer::ensureConnected() {
    return kb_.ensureConnected();
}

void KeyPacer::push(KeyJob job) {
    if (job.empty() || instance_ == nullptr)
        return;
    bool start = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastPushed_ = job;
        // No timer pending: pump now (also re-pumps a queue held while the
        // EIS device was not ready). A pending timer continues on its own.
        start = !timer_;
        for (auto& item : job)
            q_.push_back(std::move(item));
    }
    if (start)
        pump();
}

void KeyPacer::clear() {
    std::lock_guard<std::mutex> lock(m_);
    resetLocked();
}

void KeyPacer::stop() {
    std::lock_guard<std::mutex> lock(m_);
    resetLocked();
    kb_.close();
}

// Drops pending items and releases Shift if held. Never leaves the modifier
// down on the server device (engine teardown uses stop()).
void KeyPacer::resetLocked() {
    timer_.reset();
    q_.clear();
    if (shiftDown_) {
        if (kb_.ensureConnected())
            kb_.send(KEY_LEFTSHIFT, 0);
        shiftDown_ = false;
    }
}

std::atomic<bool>& KeyPacer::immediateFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

void KeyPacer::setImmediateDeliveryForTest(bool immediate) {
    immediateFlag().store(immediate, std::memory_order_release);
}

void KeyPacer::setSocketPathForTest(const std::string& path) {
    instance().kb_.setSocketPath(path);
}

KeyJob KeyPacer::lastPushedJobForTest() {
    std::lock_guard<std::mutex> lock(instance().m_);
    return instance().lastPushed_;
}

void KeyPacer::pump() {
    std::unique_lock<std::mutex> lock(m_);
    if (timer_)
        return; // a sleep is pending; its callback continues the burst
    while (!q_.empty()) {
        if (const auto* sleep = std::get_if<SleepFor>(&q_.front())) {
            const auto delay = sleep->delay;
            q_.pop_front();
            if (delay.count() == 0 || immediateFlag().load(std::memory_order_acquire))
                continue; // burst: no timer for zero delay or test immediate mode
            const uint64_t waitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(delay).count());
            const uint64_t when    = now(CLOCK_MONOTONIC) + waitUs;
            // Explicit 1ms accuracy: sd-event applies a huge default slack
            // when accuracy is 0 (measured 25-226ms late, even out of order),
            // which would randomly stretch every paced tap.
            timer_                 = instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, when, 1000, [this](EventSourceTime*, uint64_t) {
                {
                    std::lock_guard<std::mutex> inner(m_);
                    timer_.reset();
                }
                pump();
                return false;
            });
            return;
        }
        if (!kb_.isReady()) {
            // Hold the queue instead of dropping steps: start the EIS
            // handshake (no-op if already in flight); RESUMED re-pumps via
            // the resume callback. Unlocked: the handshake may poll.
            lock.unlock();
            kb_.ensureConnected();
            return;
        }
        const KeyMsg key = std::get<KeyMsg>(q_.front());
        q_.pop_front();
        bool sent = kb_.send(key.code, key.pressed);
        if (!sent && kb_.ensureConnected())
            sent = kb_.send(key.code, key.pressed);
        if (!sent)
            LOTUS_WARN("KeyPacer: failed to send key, dropping step");
        if (key.code == KEY_LEFTSHIFT)
            shiftDown_ = (key.pressed != 0);
    }
    timer_.reset();
}

} // namespace fcitx
