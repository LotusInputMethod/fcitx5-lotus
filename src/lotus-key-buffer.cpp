/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-key-buffer.h"
#include "lotus-utils.h"

bool KeyBuffer::empty() const {
    return deque_.empty();
}

bool KeyBuffer::push(KeyEntry entry) {
    if (deque_.size() >= kMaxEntries)
        return false;
    deque_.push_back(entry);
    return true;
}

KeyEntry KeyBuffer::pop() {
    KeyEntry entry = deque_.front();
    deque_.pop_front();
    return entry;
}

void KeyBuffer::clear() {
    deque_.clear();
}
