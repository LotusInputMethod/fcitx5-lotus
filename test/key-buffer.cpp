// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file key-buffer.cpp
 * @brief Unit test: KeyBuffer replays in FIFO order and drops overflow at
 *        the cap — the contract replayBufferedKeys relies on.
 *
 * Pure container, no fcitx Instance needed. Deterministic.
 */

#include "lotus-key-buffer.h"
#include "test-utils.h"

#include <cstdint>
#include <string>

namespace {
    std::string entryStr(const KeyEntry& e) {
        return "(" + std::to_string(e.sym) + "," + std::to_string(e.state) + ")";
    }

} // namespace

int main() {
    // FIFO order: pops return pushes oldest-first, sym and state preserved.
    {
        KeyBuffer buf;
        check(buf.empty(), "fresh", "empty", "not empty", "a fresh KeyBuffer must start empty so only buffered keys are replayed");
        buf.push({.sym = 101, .state = 10});
        buf.push({.sym = 102, .state = 20});
        buf.push({.sym = 103, .state = 30});
        check(!buf.empty(), "after push", "not empty", "empty", "a pushed key must be queued until the burst is complete");
        check(buf.pop().sym == 101 && buf.pop().sym == 102, "fifo order", "101,102", "out of order", "the replay must deliver keys in the order the user typed them");
        const KeyEntry last = buf.pop();
        check(last.sym == 103 && last.state == 30, "fifo tail", "(103,30)", entryStr(last), "the modifier state must survive buffering next to the keysym");
        check(buf.empty(), "drained", "empty", "not empty", "replaying every entry must drain the queue so no stale key is replayed later");
    }

    // Cap: pushes past 50 are dropped, the first 50 stay intact and ordered.
    {
        KeyBuffer buf;
        for (uint32_t i = 0; i < 60; ++i) {
            buf.push({.sym = i, .state = i * 2});
        }
        bool inOrder = true;
        for (uint32_t i = 0; i < 50; ++i) {
            const KeyEntry e = buf.pop();
            inOrder          = inOrder && e.sym == i && e.state == i * 2;
        }
        check(inOrder, "cap keeps first 50", "sym 0..49 in order", inOrder ? "ok" : "corrupted",
              "the cap must preserve the oldest 50 keys because they are the burst's real input");
        check(buf.empty(), "cap dropped overflow", "empty after 50 pops", "not empty", "keys beyond the cap must be dropped so the replay stays bounded");
    }

    // clear() discards everything.
    {
        KeyBuffer buf;
        buf.push({.sym = 7, .state = 0});
        buf.push({.sym = 8, .state = 0});
        buf.clear();
        check(buf.empty(), "clear", "empty", "not empty", "clear() must discard the pending replay immediately");
    }

    return checkExitCode("key-buffer: all checks passed");
}
