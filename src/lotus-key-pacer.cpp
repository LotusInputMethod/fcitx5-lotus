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
#include <cstdint>
#include <iterator>
#include <utility>
#include <linux/input-event-codes.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>

namespace fcitx {

    namespace {

        /**
     * @brief Explicit 1ms timer accuracy: sd-event applies a huge default slack
     * when accuracy is 0 (measured 25-226ms late, even out of order), which
     * would randomly stretch every paced tap.
     */
        constexpr uint64_t kTimerAccuracyUs = 1000;

        /**
     * @brief Settle gap (legacy server tick): the app must observe Shift-up
     * before Delete acts, otherwise it may apply Shift+Delete semantics.
     */
        constexpr std::chrono::milliseconds kSelectSettleGap{10};

        /**
     * @brief Combines one lambda per PacerItem alternative into a single
     * std::visit visitor.
     */
        template <class... Ts>
        struct Overloaded : Ts... {
            using Ts::operator()...;
        };
        template <class... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        /**
     * @brief Deadline of a monotonic timer: now + delay, in microseconds.
     */
        uint64_t monotonicDeadlineUs(std::chrono::milliseconds delay) {
            return now(CLOCK_MONOTONIC) + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(delay).count());
        }

        /**
     * @brief Arms one monotonic timer with the shared 1ms accuracy and stores
     * it in slot.
     * @param loop Event loop owning the timer.
     * @param slot Timer storage; the fire callback resets it to hand control
     * back to the pump.
     * @param delay Delay from now.
     * @param fire Invoked when the timer fires.
     */
        template <typename Fire>
        void armTimer(EventLoop& loop, std::unique_ptr<EventSourceTime>& slot, std::chrono::milliseconds delay, Fire&& fire) {
            slot = loop.addTimeEvent(CLOCK_MONOTONIC, monotonicDeadlineUs(delay), kTimerAccuracyUs, [fire = std::forward<Fire>(fire)](EventSourceTime*, uint64_t) mutable {
                fire();
                return false;
            });
        }

    } // namespace

    KeyJob buildBackspaceJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post) {
        KeyJob job;
        if (count == 0)
            return job;
        job.reserve(count * 3);
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
        job.reserve((count * 3) + 10);
        job.push_back(SleepFor{pre});
        job.push_back(KeyMsg{KEY_LEFTSHIFT, 1});
        for (size_t i = 0; i < count; ++i) {
            job.push_back(SleepFor{interval});
            job.push_back(KeyMsg{KEY_LEFT, 1});
            job.push_back(KeyMsg{KEY_LEFT, 0});
        }
        job.push_back(SleepFor{kSelectSettleGap});
        job.push_back(KeyMsg{KEY_LEFTSHIFT, 0});
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
            std::scoped_lock lock(m_);
            lastPushed_ = job;
            // No timer pending and not parked on an echo wait: pump now (also
            // re-pumps a queue held while the EIS device was not ready). A
            // pending timer or wait continues on its own.
            start = !timer_ && !waiting_;
            std::ranges::move(job, std::back_inserter(q_));
        }
        if (start)
            pump();
    }

    void KeyPacer::clear() {
        std::scoped_lock lock(m_);
        resetLocked();
    }

    void KeyPacer::stop() {
        std::scoped_lock lock(m_);
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
        std::scoped_lock lock(m_);
        barrierOwner_ = owner;
    }

    void KeyPacer::clearBarrierOwner(const LotusState* owner) {
        std::scoped_lock lock(m_);
        if (barrierOwner_ == owner)
            barrierOwner_ = nullptr;
    }

    bool KeyPacer::isWaiting() const {
        std::scoped_lock lock(m_);
        return waiting_;
    }

    bool KeyPacer::hasPendingBarrierFor(const LotusState* owner) const {
        std::scoped_lock lock(m_);
        if (owner == nullptr || barrierOwner_ != owner)
            return false;
        if (waiting_)
            return true;
        return std::ranges::any_of(q_, [](const PacerItem& item) { return std::holds_alternative<AwaitEcho>(item); });
    }

    void KeyPacer::notifyEcho(uint32_t seq) {
        bool resume = false;
        {
            std::scoped_lock lock(m_);
            if (waiting_ && seq >= waitTarget_) {
                clearWaitLocked();
                resume = true;
            }
        }
        if (resume)
            pump();
    }

    void KeyPacer::onWaitTimeout() {
        LOTUS_WARN("KeyPacer: echo wait timed out, committing anyway");
        {
            std::scoped_lock lock(m_);
            clearWaitLocked();
        }
        pump();
    }

    // Exits a parked echo wait. Caller holds m_.
    void KeyPacer::clearWaitLocked() {
        waiting_ = false;
        waitTimer_.reset();
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
        std::scoped_lock lock(instance().m_);
        return instance().lastPushed_;
    }

    void KeyPacer::pump() {
        // One visitor for every queue item kind: each handler returns Parked
        // when a timer or echo wait now owns the next move.
        const auto dispatch = Overloaded{
            [this](const KeyMsg& key) { return sendKey(key); },
            [this](const SleepFor& sleep) { return sleepStep(sleep.delay); },
            [this](const AwaitEcho& await) { return awaitStep(await); },
            [this](const Commit& commit) {
                commitStep(commit);
                return Step::Continue;
            },
        };

        while (true) {
            PacerItem item;
            {
                std::scoped_lock lock(m_);
                // A pending sleep timer or a parked echo wait owns the next
                // step; their callbacks resume pumping.
                if (timer_ || waiting_ || q_.empty())
                    return;
                item = q_.front();
                q_.pop_front();
            }
            if (item.valueless_by_exception()) {
                // Unreachable with the current noexcept-move alternatives:
                // drop the item instead of stalling the pump.
                LOTUS_WARN("KeyPacer: valueless queue item, dropping");
                continue;
            }
            if (std::visit(dispatch, item) == Step::Parked)
                return;
        }
    }

    // Sends one key tap immediately, retrying once after a reconnect. While
    // the EIS device is not ready the step is parked back at the queue head:
    // the handshake (no-op if already in flight) re-pumps via the resume
    // callback, so no paced step is dropped for readiness. Unlocked sends:
    // VirtualKeyboard has its own leaf mutex.
    KeyPacer::Step KeyPacer::sendKey(const KeyMsg& key) {
        if (!kb_.isReady()) {
            {
                std::scoped_lock lock(m_);
                q_.push_front(key);
            }
            kb_.ensureConnected();
            return Step::Parked;
        }
        bool sent = kb_.send(key.code, key.pressed);
        if (!sent && kb_.ensureConnected())
            sent = kb_.send(key.code, key.pressed);
        if (!sent)
            LOTUS_WARN("KeyPacer: failed to send key, dropping step");
        if (key.code == KEY_LEFTSHIFT) {
            std::scoped_lock lock(m_);
            shiftDown_ = (key.pressed != 0);
        }
        return Step::Continue;
    }

    // Schedules the next pump after the sleep. Zero sleeps burst (the producer
    // clamps to >= 1ms; zero is a pure ordering marker) and test immediate
    // mode skips all sleeps.
    KeyPacer::Step KeyPacer::sleepStep(std::chrono::milliseconds delay) {
        if (delay.count() == 0 || immediateFlag().load(std::memory_order_acquire))
            return Step::Continue;
        std::scoped_lock lock(m_);
        armTimer(instance_->eventLoop(), timer_, delay, [this] {
            {
                std::scoped_lock inner(m_);
                timer_.reset();
            }
            pump();
        });
        return Step::Parked;
    }

    // Parks on the echo barrier: all sends before this point are already out;
    // echoes arrive asynchronously and notifyEcho() resumes the pump once the
    // target sequence is observed (onWaitTimeout() fail-opens after the
    // timeout). An already-satisfied barrier (or no owner/loop) does not park.
    KeyPacer::Step KeyPacer::awaitStep(const AwaitEcho& await) {
        uint32_t seen = 0;
        {
            std::scoped_lock lock(m_);
            seen = (barrierOwner_ != nullptr) ? barrierOwner_->echoSequence() : 0;
            if (seen < await.targetSeq && instance_ != nullptr) {
                waiting_    = true;
                waitTarget_ = await.targetSeq;
                armTimer(instance_->eventLoop(), waitTimer_, await.timeout, [this] { onWaitTimeout(); });
                return Step::Parked;
            }
        }
        return Step::Continue;
    }

    // Fires the chain commit once. The owner is consumed (single-fire even if
    // the commit path re-pushes) and the call happens outside the pacer lock:
    // commitFromChain() re-enters push()/pump() through buffered key replay.
    void KeyPacer::commitStep(const Commit& commit) {
        LotusState* owner = nullptr;
        {
            std::scoped_lock lock(m_);
            owner         = barrierOwner_;
            barrierOwner_ = nullptr;
        }
        if (owner != nullptr)
            owner->commitFromChain(commit.text);
        else
            LOTUS_WARN("KeyPacer: Commit with no owner, skipping");
    }

} // namespace fcitx
