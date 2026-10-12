/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-key-pacer.h"
#include "lotus-state.h"
#include "lotus-utils.h"

#include <algorithm>
#include <type_traits>
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

    KeyJob buildSelectJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post, std::string text) {
        KeyJob job;
        if (count == 0)
            return job;
        job.reserve((count * 3) + 9);
        job.push_back(SleepFor{pre});
        job.push_back(KeyMsg{KEY_LEFTSHIFT, 1});
        for (size_t i = 0; i < count; ++i) {
            job.push_back(SleepFor{interval});
            job.push_back(KeyMsg{KEY_LEFT, 1});
            job.push_back(KeyMsg{KEY_LEFT, 0});
        }
        job.push_back(SleepFor{std::chrono::milliseconds{10}});
        job.push_back(KeyMsg{KEY_LEFTSHIFT, 0});
        // Settle gap (legacy server tick): the app must observe Shift-up before
        // Delete acts, otherwise it may apply Shift+Delete semantics.
        job.push_back(SleepFor{interval});
        job.push_back(KeyMsg{KEY_DELETE, 1});
        job.push_back(KeyMsg{KEY_DELETE, 0});
        // Chain-visible commit point: the echo handler counts every echo
        // (selection Lefts 1..E plus this Delete as E+1) and notifies; the pump
        // parks here until the target sequence is observed, then fires the
        // commit. No trailing tap: anything sent after Delete would leak to the
        // app (the off-by-one fixed earlier).
        job.push_back(SleepFor{post});
        job.push_back(AwaitEcho{static_cast<uint32_t>(count) + 1, kAwaitEchoTimeout});
        job.push_back(Commit{std::move(text)});
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
            // No timer pending and not parked on an echo wait: pump now (also
            // re-pumps a queue held while the EIS device was not ready). A
            // pending timer or wait continues on its own.
            start = !timer_ && !waiting_;
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
    // down on the server device (engine teardown uses stop()). Also disarms
    // any parked echo wait and drops its owner: queue lifetime is tied to the
    // replacement state, so every abort path funnels through here.
    void KeyPacer::resetLocked() {
        timer_.reset();
        waitTimer_.reset();
        waiting_      = false;
        barrierOwner_ = nullptr;
        q_.clear();
        if (shiftDown_) {
            if (kb_.ensureConnected())
                kb_.send(KEY_LEFTSHIFT, 0);
            shiftDown_ = false;
        }
    }

    void KeyPacer::setBarrierOwner(LotusState* owner) {
        std::lock_guard<std::mutex> lock(m_);
        barrierOwner_ = owner;
    }

    void KeyPacer::clearBarrierOwner(const LotusState* owner) {
        std::lock_guard<std::mutex> lock(m_);
        if (barrierOwner_ == owner)
            barrierOwner_ = nullptr;
    }

    bool KeyPacer::isWaiting() const {
        std::lock_guard<std::mutex> lock(m_);
        return waiting_;
    }

    bool KeyPacer::hasPendingBarrierFor(const LotusState* owner) const {
        std::lock_guard<std::mutex> lock(m_);
        if (owner == nullptr || barrierOwner_ != owner)
            return false;
        if (waiting_)
            return true;
        return std::any_of(q_.begin(), q_.end(), [](const PacerItem& item) { return std::holds_alternative<AwaitEcho>(item); });
    }

    void KeyPacer::notifyEcho(uint32_t seq) {
        bool resume = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (waiting_ && seq >= waitTarget_) {
                waiting_ = false;
                waitTimer_.reset();
                resume = true;
            }
        }
        if (resume)
            pump();
    }

    void KeyPacer::onWaitTimeout() {
        LOTUS_WARN("KeyPacer: echo wait timed out, committing anyway");
        {
            std::lock_guard<std::mutex> lock(m_);
            waiting_ = false;
            waitTimer_.reset();
        }
        pump();
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
        // PacerItem alternative indexes dispatched by the switch below.
        // Pinned so reordering the variant breaks loudly instead of silently
        // misdispatching.
        static_assert(std::is_same_v<std::variant_alternative_t<0, PacerItem>, KeyMsg>);
        static_assert(std::is_same_v<std::variant_alternative_t<1, PacerItem>, SleepFor>);
        static_assert(std::is_same_v<std::variant_alternative_t<2, PacerItem>, AwaitEcho>);
        static_assert(std::is_same_v<std::variant_alternative_t<3, PacerItem>, Commit>);

        while (true) {
            PacerItem item;
            {
                std::lock_guard<std::mutex> lock(m_);
                // A pending sleep timer or a parked echo wait owns the next
                // step; their callbacks resume pumping.
                if (timer_ || waiting_ || q_.empty())
                    return;
                item = q_.front();
                q_.pop_front();
            }
            switch (item.index()) {
                case 0: { // KeyMsg
                    const KeyMsg key = std::get<0>(item);
                    if (!kb_.isReady()) {
                        // Hold the queue instead of dropping steps: start the EIS
                        // handshake (no-op if already in flight); RESUMED re-pumps via
                        // the resume callback. Unlocked: the handshake may poll.
                        {
                            std::lock_guard<std::mutex> lock(m_);
                            q_.push_front(key);
                        }
                        kb_.ensureConnected();
                        return;
                    }
                    bool sent = kb_.send(key.code, key.pressed);
                    if (!sent && kb_.ensureConnected())
                        sent = kb_.send(key.code, key.pressed);
                    if (!sent)
                        LOTUS_WARN("KeyPacer: failed to send key, dropping step");
                    if (key.code == KEY_LEFTSHIFT) {
                        std::lock_guard<std::mutex> lock(m_);
                        shiftDown_ = (key.pressed != 0);
                    }
                    break;
                }
                case 1: { // SleepFor
                    const auto delay = std::get<1>(item).delay;
                    if (delay.count() == 0 || immediateFlag().load(std::memory_order_acquire))
                        continue; // burst: no timer for zero delay or test immediate mode
                    const uint64_t waitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(delay).count());
                    const uint64_t when   = now(CLOCK_MONOTONIC) + waitUs;
                    // Explicit 1ms accuracy: sd-event applies a huge default slack
                    // when accuracy is 0 (measured 25-226ms late, even out of order),
                    // which would randomly stretch every paced tap.
                    std::lock_guard<std::mutex> lock(m_);
                    timer_ = instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, when, 1000, [this](EventSourceTime*, uint64_t) {
                        {
                            std::lock_guard<std::mutex> inner(m_);
                            timer_.reset();
                        }
                        pump();
                        return false;
                    });
                    return;
                }
                case 2: { // AwaitEcho
                    // All sends before this point are already out; echoes arrive
                    // asynchronously. Park here: the echo handler resumes via
                    // notifyEcho once the target sequence is observed.
                    const uint32_t targetSeq = std::get<2>(item).targetSeq;
                    const auto     timeout   = std::get<2>(item).timeout;
                    uint32_t       seen      = 0;
                    {
                        std::lock_guard<std::mutex> lock(m_);
                        seen = (barrierOwner_ != nullptr) ? barrierOwner_->echoSequence() : 0;
                        if (seen < targetSeq && instance_ != nullptr) {
                            waiting_              = true;
                            waitTarget_           = targetSeq;
                            const uint64_t waitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
                            const uint64_t when   = now(CLOCK_MONOTONIC) + waitUs;
                            waitTimer_            = instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, when, 1000, [this](EventSourceTime*, uint64_t) {
                                onWaitTimeout();
                                return false;
                            });
                            return;
                        }
                    }
                    continue; // already satisfied (or no owner/loop): don't park
                }
                case 3: { // Commit
                    LotusState* owner = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(m_);
                        owner         = barrierOwner_;
                        barrierOwner_ = nullptr; // single-fire even if it re-pushes
                    }
                    if (owner != nullptr)
                        owner->commitFromChain(std::get<3>(item).text);
                    else
                        LOTUS_WARN("KeyPacer: Commit with no owner, skipping");
                    continue;
                }
                default: {
                    // Unreachable unless the variant is valueless by exception:
                    // drop the item instead of stalling the pump.
                    LOTUS_WARN("KeyPacer: unknown queue item, dropping");
                    break;
                }
            }
        }
    }

} // namespace fcitx
