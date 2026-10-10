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

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <variant>
#include <vector>

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

class KeyPacer {
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

  private:
    void pump();

    VirtualKeyboard                kb_;
    Instance*                      instance_{nullptr};
    std::deque<PacerItem>          q_;
    std::unique_ptr<EventSourceTime> timer_;
    bool                           shiftDown_{false};
};

} // namespace fcitx

#endif // _FCITX5_LOTUS_KEY_PACER_H_
