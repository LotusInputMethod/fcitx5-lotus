// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file kb-sender.cpp
 * @brief Concurrency test for the shared keyboard request sender.
 *
 * The fcitx thread and the async scheduler thread both send bursts through KbSender, so two threads
 * writing at the same time must never interleave bytes: the listener has to receive complete messages.
 */

#include "kb-socket-listener.h"
#include "lotus-kb-sender.h"
#include "lotus-utils.h"
#include "test-input-context.h"

#include <cstdio>
#include <thread>

int main() {
    configureTestPaths("fcitx5-lotus-kb-sender");

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    KbSender    sender(buildSocketPath("kbSocket"));
    std::thread first([&sender] { sender.send(KbMsg{.op = KbOp::Backspace, .count = 3, .preDelay = 0, .interval = 7, .postDelay = 0}); });
    std::thread second([&sender] { sender.send(KbMsg{.op = KbOp::Select, .count = 5, .preDelay = 0, .interval = 9, .postDelay = 0}); });
    first.join();
    second.join();

    KbMsg a{};
    KbMsg b{};
    if (!listener.receive(a, "first request from the sender threads") || !listener.receive(b, "second request from the sender threads"))
        return 1;

    const bool backspaceSeen = (a.op == KbOp::Backspace && a.count == 3 && a.interval == 7) || (b.op == KbOp::Backspace && b.count == 3 && b.interval == 7);
    const bool selectSeen    = (a.op == KbOp::Select && a.count == 5 && a.interval == 9) || (b.op == KbOp::Select && b.count == 5 && b.interval == 9);
    if (!backspaceSeen || !selectSeen) {
        std::printf("FAIL both bursts arrive intact\n");
        return 1;
    }

    std::printf("lotus kb sender: all checks passed\n");
    return 0;
}
