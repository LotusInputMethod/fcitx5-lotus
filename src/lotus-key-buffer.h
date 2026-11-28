/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
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
class LOTUS_EXPORT KeyBuffer {
  public:
    /**
     * @brief Whether when no key entry in deque
     * @return true when deque is empty
     */
    [[nodiscard]] bool empty() const;

    /// @brief Number of keys still waiting for replay.
    [[nodiscard]] size_t size() const {
        return deque_.size();
    }

    /**
     * @brief Appends a keystroke if there is room.
     * @param entry Key entry pushed into deque
     * @return True if queued, false if the buffer was full.
     */
    bool push(KeyEntry entry);

    /**
     * @brief Removes and returns the oldest keystroke.
     * @return the first entry of deque
     */
    KeyEntry pop();

    /**
     * @brief Discards every buffered keystroke.
     */
    void clear();

  private:
    static constexpr size_t kMaxEntries = 50; ///< Same cap as the old inline checks.
    std::deque<KeyEntry>    deque_;
};

#endif // _FCITX5_LOTUS_KEY_BUFFER_H_
