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

#include <cstdint>
#include <iostream>
#include <string>

namespace {

    int  failures = 0;

    void check(bool ok, const std::string& step, const std::string& expected, const std::string& actual) {
        if (ok)
            return;
        ++failures;
        std::cerr << "Step: " << step << "\nExpected: " << expected << "\nActual: " << actual << "\n";
    }

    std::string entryStr(const KeyEntry& e) {
        return "(" + std::to_string(e.sym) + "," + std::to_string(e.state) + ")";
    }

} // namespace

int main() {
    // FIFO order: pops return pushes oldest-first, sym and state preserved.
    {
        KeyBuffer buf;
        check(buf.empty(), "fresh", "empty", "not empty");
        buf.push({.sym = 101, .state = 10});
        buf.push({.sym = 102, .state = 20});
        buf.push({.sym = 103, .state = 30});
        check(!buf.empty(), "after push", "not empty", "empty");
        check(buf.pop().sym == 101 && buf.pop().sym == 102, "fifo order", "101,102", "out of order");
        const KeyEntry last = buf.pop();
        check(last.sym == 103 && last.state == 30, "fifo tail", "(103,30)", entryStr(last));
        check(buf.empty(), "drained", "empty", "not empty");
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
        check(inOrder, "cap keeps first 50", "sym 0..49 in order", inOrder ? "ok" : "corrupted");
        check(buf.empty(), "cap dropped overflow", "empty after 50 pops", "not empty");
    }

    // clear() discards everything.
    {
        KeyBuffer buf;
        buf.push({.sym = 7, .state = 0});
        buf.push({.sym = 8, .state = 0});
        buf.clear();
        check(buf.empty(), "clear", "empty", "not empty");
    }

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "key-buffer: all checks passed\n";
    return 0;
}
