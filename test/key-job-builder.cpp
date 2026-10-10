// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file key-job-builder.cpp
 * @brief Unit test: buildBackspaceJob/buildSelectJob expand replacements into
 *        the exact KeyMsg/SleepFor stream the old server pacing produced.
 *
 * Pure functions, no fcitx Instance needed. Deterministic: no timers fire.
 */

#include "lotus-key-pacer.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <linux/input-event-codes.h>

namespace {

    int  failures = 0;

    void check(bool ok, const std::string& step, const std::string& expected, const std::string& actual) {
        if (ok)
            return;
        ++failures;
        std::cerr << "Step: " << step << "\nExpected: " << expected << "\nActual: " << actual << "\n";
    }

    std::string itemStr(const fcitx::PacerItem& item) {
        if (const auto* key = std::get_if<fcitx::KeyMsg>(&item))
            return "Key(" + std::to_string(key->code) + "," + std::to_string(key->pressed) + ")";
        if (const auto* sleep = std::get_if<fcitx::SleepFor>(&item))
            return "Sleep(" + std::to_string(sleep->delay.count()) + "ms)";
        if (const auto* wait = std::get_if<fcitx::AwaitEcho>(&item))
            return "AwaitEcho(" + std::to_string(wait->targetSeq) + ")";
        if (const auto* commit = std::get_if<fcitx::Commit>(&item))
            return "Commit('" + commit->text + "')";
        return "?";
    }

    std::string jobStr(const fcitx::KeyJob& job) {
        std::string out = "[";
        for (size_t i = 0; i < job.size(); ++i) {
            if (i > 0)
                out += ", ";
            out += itemStr(job[i]);
        }
        return out + "]";
    }

    bool isKey(const fcitx::PacerItem& item, uint16_t code, uint8_t pressed) {
        const auto* key = std::get_if<fcitx::KeyMsg>(&item);
        return key != nullptr && key->code == code && key->pressed == pressed;
    }

    bool isSleep(const fcitx::PacerItem& item, int64_t ms) {
        const auto* sleep = std::get_if<fcitx::SleepFor>(&item);
        return sleep != nullptr && sleep->delay.count() == ms;
    }

    bool isAwait(const fcitx::PacerItem& item, uint32_t target) {
        const auto* wait = std::get_if<fcitx::AwaitEcho>(&item);
        return wait != nullptr && wait->targetSeq == target;
    }

    bool isCommit(const fcitx::PacerItem& item, const std::string& text) {
        const auto* commit = std::get_if<fcitx::Commit>(&item);
        return commit != nullptr && commit->text == text;
    }

} // namespace

int main() {
    using namespace std::chrono_literals;

    // Backspace N=2, pre=0, interval=17, post=33: last gap uses max(17,33)=33.
    {
        auto job = fcitx::buildBackspaceJob(2, 0ms, 17ms, 33ms);
        check(job.size() == 6, "bs2 size", "6", std::to_string(job.size()));
        if (job.size() == 6) {
            check(isSleep(job[0], 0), "bs2[0]", "Sleep(0ms)", itemStr(job[0]));
            check(isKey(job[1], KEY_BACKSPACE, 1), "bs2[1]", "Key(BS,press)", itemStr(job[1]));
            check(isKey(job[2], KEY_BACKSPACE, 0), "bs2[2]", "Key(BS,release)", itemStr(job[2]));
            check(isSleep(job[3], 33), "bs2[3]", "Sleep(33ms)=max(interval,post)", itemStr(job[3]));
            check(isKey(job[4], KEY_BACKSPACE, 1), "bs2[4]", "Key(BS,press)", itemStr(job[4]));
            check(isKey(job[5], KEY_BACKSPACE, 0), "bs2[5]", "Key(BS,release)", itemStr(job[5]));
        }
    }

    // Backspace N=1: single tap, only pre sleep, no interval.
    {
        auto job = fcitx::buildBackspaceJob(1, 20ms, 17ms, 33ms);
        check(job.size() == 3, "bs1 size", "3", std::to_string(job.size()));
        if (job.size() == 3) {
            check(isSleep(job[0], 20), "bs1[0]", "Sleep(20ms)", itemStr(job[0]));
            check(isKey(job[1], KEY_BACKSPACE, 1), "bs1[1]", "Key(BS,press)", itemStr(job[1]));
            check(isKey(job[2], KEY_BACKSPACE, 0), "bs1[2]", "Key(BS,release)", itemStr(job[2]));
        }
    }

    // Backspace N=3, interval wins over post for middle gap.
    {
        auto job = fcitx::buildBackspaceJob(3, 5ms, 50ms, 10ms);
        check(job.size() == 9, "bs3 size", "9", std::to_string(job.size()));
        if (job.size() == 9) {
            check(isSleep(job[0], 5), "bs3 pre", "Sleep(5ms)", itemStr(job[0]));
            check(isSleep(job[3], 50), "bs3 mid", "Sleep(50ms)=interval", itemStr(job[3]));
            check(isSleep(job[6], 50), "bs3 last", "Sleep(50ms)=max(50,10)", itemStr(job[6]));
        }
    }

    // Backspace N=0: empty job, nothing queued.
    {
        auto job = fcitx::buildBackspaceJob(0, 0ms, 17ms, 33ms);
        check(job.empty(), "bs0 empty", "empty", jobStr(job));
    }

    // Select N=2: Shift-down, 2x Left, 10ms, Shift-up, Sleep(interval),
    // Delete, Sleep(post), AwaitEcho{3}, Commit. No trailing tap:
    // anything sent after Delete would leak as an extra cursor-left.
    {
        auto job = fcitx::buildSelectJob(2, 5ms, 17ms, 33ms, "AB");
        check(job.size() == 16, "sel2 size", "16", std::to_string(job.size()));
        if (job.size() == 16) {
            check(isSleep(job[0], 5), "sel2[0]", "Sleep(5ms)=pre", itemStr(job[0]));
            check(isKey(job[1], KEY_LEFTSHIFT, 1), "sel2[1]", "Shift-down", itemStr(job[1]));
            check(isSleep(job[2], 17) && isKey(job[3], KEY_LEFT, 1) && isKey(job[4], KEY_LEFT, 0), "sel2 first Left", "Sleep(17)+Left tap", jobStr(job));
            check(isSleep(job[5], 17) && isKey(job[6], KEY_LEFT, 1) && isKey(job[7], KEY_LEFT, 0), "sel2 second Left", "Sleep(17)+Left tap", jobStr(job));
            check(isSleep(job[8], 10), "sel2[8]", "Sleep(10ms) before Shift-up", itemStr(job[8]));
            check(isKey(job[9], KEY_LEFTSHIFT, 0), "sel2[9]", "Shift-up", itemStr(job[9]));
            check(isSleep(job[10], 17), "sel2[10]", "Sleep(17ms)=settle before Delete", itemStr(job[10]));
            check(isKey(job[11], KEY_DELETE, 1) && isKey(job[12], KEY_DELETE, 0), "sel2 Del", "Delete tap", jobStr(job));
            check(isSleep(job[13], 33), "sel2 post", "Sleep(33ms)=post", itemStr(job[13]));
            check(isAwait(job[14], 3), "sel2[14]", "AwaitEcho{3}=2 Lefts+Delete", itemStr(job[14]));
            check(isCommit(job[15], "AB"), "sel2[15]", "Commit(AB)", itemStr(job[15]));
        }
    }

    // Select N=1 shape + empty case.
    {
        auto job = fcitx::buildSelectJob(1, 0ms, 20ms, 25ms, "AB");
        check(job.size() == 13, "sel1 size", "13", std::to_string(job.size()));
        if (job.size() == 13) {
            check(isKey(job[1], KEY_LEFTSHIFT, 1) && isKey(job[6], KEY_LEFTSHIFT, 0), "sel1 shift flanks", "down@1 up@6", jobStr(job));
            check(isSleep(job[7], 20), "sel1[7]", "Sleep(20ms)=settle", itemStr(job[7]));
            check(isKey(job[8], KEY_DELETE, 1) && isKey(job[9], KEY_DELETE, 0), "sel1 Del", "Delete tap@8-9", jobStr(job));
            check(isSleep(job[10], 25), "sel1 post", "Sleep(25ms)@10", itemStr(job[10]));
            check(isAwait(job[11], 2), "sel1[11]", "AwaitEcho{2}", itemStr(job[11]));
            check(isCommit(job[12], "AB"), "sel1[12]", "Commit(AB)", itemStr(job[12]));
        }
        auto empty = fcitx::buildSelectJob(0, 0ms, 20ms, 25ms, "AB");
        check(empty.empty(), "sel0 empty", "empty", jobStr(empty));
    }

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "key-job-builder: all checks passed\n";
    return 0;
}
