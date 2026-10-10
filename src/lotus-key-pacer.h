/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
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
#include <variant>
#include <vector>

#include <linux/input-event-codes.h>

namespace fcitx {

class Instance;
class EventSourceTime;

/**
 * @brief A pause in the pacer stream. Producer clamps to [1ms,1000ms];
 *        zero durations are skipped (burst) and never scheduled.
 */
struct SleepFor {
    std::chrono::milliseconds delay;
};

using PacerItem = std::variant<KeyMsg, SleepFor>;
using KeyJob    = std::vector<PacerItem>;

/**
 * @brief Pure builders: expand a replacement into a pacer job.
 *
 * Backspace N: [Sleep(pre), tap x N] with Sleep(max(interval,post)) before
 * the final tap. Select N: Shift-down, N x Left, 10ms, Shift-up, Delete,
 * post, Left (legacy tail, byte-for-byte with the old server pacing).
 * Zero sleeps are kept in the job; pump() skips them (burst).
 */
KeyJob LOTUS_EXPORT buildBackspaceJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post);
KeyJob LOTUS_EXPORT buildSelectJob(size_t count, std::chrono::milliseconds pre, std::chrono::milliseconds interval, std::chrono::milliseconds post);

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
    // pump() locks m_ for its whole run: timer callbacks (loop thread) and
    // push() (keyEvent thread, or test main thread) never interleave.
    // VirtualKeyboard has its own leaf mutex; holding m_ across send is safe.
    void pump();
    // resetLocked() requires m_ held. Shared by clear()/stop() so teardown
    // also releases Shift instead of leaving it stuck down.
    void resetLocked();

    static std::atomic<bool>& immediateFlag();

    VirtualKeyboard                kb_;
    Instance*                      instance_{nullptr};
    std::deque<PacerItem>          q_;
    std::unique_ptr<EventSourceTime> timer_;
    std::mutex                     m_;
    KeyJob                         lastPushed_; ///< test snapshot (sleeps included)
    bool                           shiftDown_{false};
};

} // namespace fcitx

#endif // _FCITX5_LOTUS_KEY_PACER_H_
