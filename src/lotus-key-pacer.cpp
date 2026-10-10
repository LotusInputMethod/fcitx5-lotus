/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-key-pacer.h"
#include "lotus-utils.h"

#include <linux/input-event-codes.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>

namespace fcitx {

KeyPacer& KeyPacer::instance() {
    static KeyPacer pacer;
    return pacer;
}

KeyPacer::KeyPacer()  = default;
KeyPacer::~KeyPacer() = default;

void KeyPacer::init(Instance* instance) {
    instance_ = instance;
}

void KeyPacer::push(KeyJob job) {
    if (job.empty() || instance_ == nullptr)
        return;
    const bool wasIdle = q_.empty() && !timer_;
    for (auto& item : job)
        q_.push_back(std::move(item));
    if (wasIdle)
        pump();
}

void KeyPacer::clear() {
    timer_.reset();
    q_.clear();
    if (shiftDown_) {
        if (kb_.ensureConnected())
            kb_.send(KEY_LEFTSHIFT, 0);
        shiftDown_ = false;
    }
}

void KeyPacer::stop() {
    timer_.reset();
    q_.clear();
    shiftDown_ = false;
}

void KeyPacer::pump() {
    while (!q_.empty()) {
        if (const auto* sleep = std::get_if<SleepFor>(&q_.front())) {
            const auto delay = sleep->delay;
            q_.pop_front();
            if (delay.count() == 0)
                continue; // burst: no timer for zero delay
            const uint64_t waitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(delay).count());
            const uint64_t when    = now(CLOCK_MONOTONIC) + waitUs;
            timer_                 = instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, when, 0, [this](EventSourceTime*, uint64_t) {
                timer_.reset();
                pump();
                return false;
            });
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
