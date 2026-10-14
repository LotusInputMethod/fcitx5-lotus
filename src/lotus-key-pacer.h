/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-key-pacer.h
 * @brief Paces KeyMsg delivery with fcitx EventLoop timers — no new thread.
 *
 * Queue holds a flexible stream of items: either an operation (KeyMsg to
 * send immediately) or a sleep. pump() sends burst KeyMsgs until it meets a
 * SleepFor, then reschedules itself recursively after that delay.
 */

#ifndef _FCITX5_LOTUS_KEY_PACER_H_
#define _FCITX5_LOTUS_KEY_PACER_H_

#include "lotus-protocol.h"
#include "lotus-virtual-keyboard.h"
#include "lotus-export.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include <linux/input-event-codes.h>

namespace fcitx {

    class Instance;
    class EventSourceTime;
    class LotusState;

    /**
 * @brief A pause in the pacer stream. Producer clamps to [1ms,1000ms];
 *        zero durations are skipped (burst) and never scheduled.
 */
    struct SleepFor {
        std::chrono::milliseconds delay;
    };

    static constexpr std::chrono::milliseconds kAwaitEchoTimeout{1000};

    /**
 * @brief Barrier: pause pumping until enough echoes arrived.
 *
 * The pump parks on this item (no timer) and resumes once the echo handler
 * reports an echo sequence >= targetSeq via notifyEcho(). The timeout is a
 * fail-open fallback (commit anyway + warn) so a lost echo cannot stall the
 * queue forever. Echoes prove fcitx saw the keys, not that the app processed
 * them; see the plan's honest limitation.
 */
    struct AwaitEcho {
        uint32_t                  targetSeq;
        std::chrono::milliseconds timeout = kAwaitEchoTimeout;
    };

    /**
 * @brief Commit point: fire the owner's commit with the carried text, then
 * continue pumping.
 *
 * The text rides the chain (copied from the replacement at build time) so
 * the order AND the payload are both visible in the job. The pump still
 * cannot touch InputContext itself: the actual commit runs on the barrier
 * owner (LotusState) set by the replacement that pushed the job.
 */
    struct Commit {
        std::string text;
    };

    using PacerItem = std::variant<KeyMsg, SleepFor, AwaitEcho, Commit>;
    using KeyJob    = std::vector<PacerItem>;

    /**
 * @brief Pure builders: expand a replacement into a pacer job.
 *
 * Backspace N: [Sleep(pre), tap x N, AwaitEcho{N}, Commit{text}] with
 * Sleep(max(interval,post)) before the final tap. Select N: Shift-down,
 * N x Left taps, 10ms, Shift-up, Sleep(interval), Delete, Sleep(post),
 * AwaitEcho{N+1}, Commit{text}.
 * Both tails share one contract: the echo handler counts every echo and
 * notifies; the pump parks on the barrier until the full echo sequence is
 * observed, then fires the chain commit (fail-open after the timeout). The
 * final echo is swallowed by the echo handler, so the app sees exactly
 * count-1 forwarded taps plus the commit — the barrier target counts the
 * swallowed echo too. No trailing tap beyond the deleted range: anything
 * sent after it would leak to the app.
 * Zero sleeps are kept in the job; pump() skips them (burst).
 */
    KeyJob LOTUS_EXPORT buildBackspaceJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post, std::string text);
    KeyJob LOTUS_EXPORT buildSelectJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post, std::string text);

    class LOTUS_EXPORT KeyPacer {
      public:
        /**
     * @brief Global pacer (single FIFO keeps the old additive ordering).
     */
        static KeyPacer& instance();

        KeyPacer();  // defined in cpp where EventSourceTime is complete
        ~KeyPacer(); // defined in cpp where EventSourceTime is complete

        KeyPacer(const KeyPacer&)            = delete;
        KeyPacer& operator=(const KeyPacer&) = delete;
        KeyPacer(KeyPacer&&)                 = delete;
        KeyPacer& operator=(KeyPacer&&)      = delete;

        /**
     * @brief Binds the fcitx event loop. Called once from LotusEngine ctor.
     */
        void init(Instance* instance);

        /**
     * @brief Ensures the kb_socket connection (best effort). Main loop only.
     * @return True if the server is reachable.
     */
        bool ensureConnected();

        /**
     * @brief Appends a job and starts pumping if idle. Main loop only.
     */
        void push(KeyJob job);

        /**
     * @brief Drops pending items and releases Shift if held. Main loop only.
     */
        void clear();

        /**
     * @brief Cancels the timer and drops pending items. Main loop only.
     */
        void stop();

        /**
     * @brief Sets the replacement owner for barrier waits and chain commits.
     *
     * Set by performReplacement before pushing (cleared on completion, abort
     * or owner destruction). The pump queries it for the current echo
     * sequence at park time and invokes its commit when a CommitPending item
     * is pumped. Main loop only.
     */
        void setBarrierOwner(LotusState* owner);

        /**
     * @brief Clears the barrier owner if it is the given one (owner teardown
     * or replacement abort paths). Main loop only.
     */
        void clearBarrierOwner(const LotusState* owner);

        /**
     * @brief Whether the pump is parked on an AwaitEcho wait. Lets the stale
     * replacement guard tell "waiting for echoes" apart from stale state.
     */
        [[nodiscard]] bool isWaiting() const;

        /**
     * @brief Whether this owner still has an unresolved chain barrier: the
     * pump is parked on its AwaitEcho wait, or an AwaitEcho item for it is
     * still queued (pump mid-job, e.g. in a post-Delete sleep). Lets the
     * stale replacement guard avoid nuking a live barrier whose echoes are
     * still in flight. Main loop only.
     */
        [[nodiscard]] bool hasPendingBarrierFor(const LotusState* owner) const;

        /**
     * @brief Reports one counted echo (echo sequence number). Resumes a
     * parked AwaitEcho wait whose target is met. Safe to call with no wait
     * pending (no-op). Main loop only.
     */
        void notifyEcho(uint32_t seq);

        /**
     * @brief Test hook: when true, pump() skips all sleeps and delivers the
     * whole job synchronously inside push(). Headless tests use this so no
     * EventLoop is needed (fcitx timers can't be pumped deterministically
     * off the loop thread, and the loop can't be restarted after exit()).
     */
        static void setImmediateDeliveryForTest(bool immediate);
        /**
     * @brief Test hook: redirect the kb socket path (default kb_socket).
     */
        static void setSocketPathForTest(const std::string& path);
        /**
     * @brief Test hook: snapshot of the last pushed job (sleeps included,
     * so tests can assert pre/interval/post values without wall-clock).
     */
        static KeyJob lastPushedJobForTest();

      private:
        // pump() uses short critical sections (never holds m_ across sends,
        // callbacks or waits): notifyEcho (echo handler) → commit callback →
        // replay → push → pump must not self-deadlock on this mutex.
        // VirtualKeyboard has its own leaf mutex; lock order is always
        // KeyPacer m_ (briefly) then VirtualKeyboard m_.
        void pump();
        // waitTimer_ callback: fail-open resume on lost echoes.
        void onWaitTimeout();
        // resetLocked() requires m_ held. Shared by clear()/stop() so teardown
        // also releases Shift instead of leaving it stuck down.
        void resetLocked();

        /**
     * @brief Outcome of one pump step: Continue keeps draining the queue,
     * Parked means a timer or echo wait now owns the next step.
     */
        enum class Step {
            Continue,
            Parked
        };

        // One handler per PacerItem alternative; each returns Parked when a
        // timer or echo wait now owns the next move.

        /**
     * @brief Sends one key tap, retrying once after a reconnect. Parks the
     * step back at the queue head while the EIS device is not ready.
     */
        [[nodiscard]] Step sendKey(const KeyMsg& key);

        /**
     * @brief Arms the next pump after a sleep; zero sleeps burst instead.
     */
        [[nodiscard]] Step sleepStep(std::chrono::milliseconds delay);

        /**
     * @brief Parks on an echo barrier until notifyEcho() or the fail-open
     * timeout resumes the pump.
     */
        [[nodiscard]] Step awaitStep(const AwaitEcho& await);

        /**
     * @brief Fires the chain commit once and consumes the barrier owner.
     */
        void commitStep(const Commit& commit);

        /**
     * @brief Exits a parked echo wait. Caller holds m_.
     */
        void                             clearWaitLocked();

        static std::atomic<bool>&        immediateFlag();

        VirtualKeyboard                  kb_;
        Instance*                        instance_{nullptr};
        std::deque<PacerItem>            q_;
        std::unique_ptr<EventSourceTime> timer_;
        std::unique_ptr<EventSourceTime> waitTimer_;
        LotusState*                      barrierOwner_{nullptr};
        mutable std::mutex               m_;
        KeyJob                           lastPushed_; ///< test snapshot (sleeps included)
        uint32_t                         waitTarget_{0};
        bool                             waiting_{false};
        bool                             shiftDown_{false};
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_KEY_PACER_H_
