/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-key-buffer.h
 * @brief Bounded FIFO holding keystrokes typed while a replacement is in
 *        flight.
 *
 * LotusState swallows the keys a user types during a pacer-driven deletion
 * and replays them through the engine once the replacement commits. Keys
 * pushed while full are dropped: they were already swallowed, so buffering
 * past the cap would only delay the keys it can still save.
 */

#ifndef _FCITX5_LOTUS_KEY_BUFFER_H_
#define _FCITX5_LOTUS_KEY_BUFFER_H_

#include "lotus-utils.h"

#include <cstddef>
#include <deque>

/**
 * @brief Bounded FIFO of keystrokes awaiting replay.
 */
class KeyBuffer {
  public:
    /**
     * @brief True when no keystroke is waiting for replay.
     */
    bool empty() const {
        return queue_.empty();
    }

    /**
     * @brief Appends a keystroke; silently dropped when the buffer is full.
     */
    void push(KeyEntry entry) {
        if (queue_.size() >= kMaxEntries)
            return;
        queue_.push_back(entry);
    }

    /**
     * @brief Removes and returns the oldest keystroke.
     * @pre !empty()
     */
    KeyEntry pop() {
        KeyEntry entry = queue_.front();
        queue_.pop_front();
        return entry;
    }

    /**
     * @brief Discards every buffered keystroke.
     */
    void clear() {
        queue_.clear();
    }

  private:
    static constexpr size_t kMaxEntries = 50; ///< Same cap as the old inline checks.

    std::deque<KeyEntry>    queue_;
};

#endif // _FCITX5_LOTUS_KEY_BUFFER_H_
