// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file replacement-delays.cpp
 * @brief Headless regression test: the global default delays reach the pacer
 *        job (interval = DefaultBackspaceInterval, post = DefaultPostDelay)
 *        and DefaultCommitInterval delays a replacement after a recent commit.
 *
 * Runs KeyPacer in immediate mode against a mini EIS server: job shapes are
 * inspected via lastPushedJobForTest (no wall-clock involved), while echoed
 * backspaces are re-sent through the engine on loop timers — accepted means
 * swallowed, forwarded means passed through.
 */

#include "mini-eis-fixture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

    const std::string kPath("lotus-test-replacement-delays-kb");

    int               failures = 0;

    void              check(bool ok, const std::string& step, const std::string& detail) {
        if (ok)
            return;
        ++failures;
        std::cerr << "FAIL: " << step << " -- " << detail << "\n";
    }

    bool expectSend(lotus_test::EisFixture& fix, fcitx::KeySym symbol, bool requireAccepted, const char* meaning) {
        const bool accepted = fix.send(symbol);
        if (accepted != requireAccepted) {
            check(false, "process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(accepted)) + ", " + fix.commitList() + " -- " + meaning);
            return false;
        }
        return true;
    }

    std::vector<int64_t> sleepMillis(const fcitx::KeyJob& job) {
        std::vector<int64_t> out;
        for (const auto& item : job) {
            if (const auto* sleep = std::get_if<fcitx::SleepFor>(&item))
                out.push_back(sleep->delay.count());
        }
        return out;
    }

    bool expectTaps(size_t want, const char* step, const char* meaning) {
        size_t      taps = 0;
        std::string detail;
        if (!lotus_test::countJobTaps(fcitx::KeyPacer::instance().lastPushedJobForTest(), KEY_BACKSPACE, taps, detail) || taps != want) {
            check(false, step, "want=" + std::to_string(want) + " taps, " + detail + " -- " + meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-replacement-delays");
    lotus_test::EisFixture fix;
    fix.engine = std::make_unique<fcitx::LotusEngine>(&fix.testInstance.instance);
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DefaultBackspaceInterval", "17");
    config.setValueByPath("DefaultPostDelay", "33");
    config.setValueByPath("DefaultCommitInterval", "1000");
    fix.engine->setConfig(config);
    if (fix.engine->config().mode.value() != fcitx::LotusMode::Smooth || *fix.engine->config().defaultBackspaceInterval != 17 || *fix.engine->config().defaultPostDelay != 33 ||
        *fix.engine->config().defaultCommitInterval != 1000) {
        check(false, "configure Smooth/Telex and default delays", "configured values differ");
        return 1;
    }
    fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);
    if (!fix.startEis(kPath)) {
        check(false, "start mini EIS server", "bind failed");
        return 1;
    }
    fix.context = std::make_unique<TestInputContext>(&fix.testInstance.instance);
    fix.context->focusIn();
    fcitx::InputContextEvent focus(fix.context.get(), fcitx::EventType::InputContextFocusIn);
    fix.engine->activate(fix.entry, focus);
    fix.context->resetPreeditUpdateCount();

    // Telex a, s changes the real Bamboo preedit a -> á; Smooth mode replaces
    // the old character through the pacer job.
    if (!expectSend(fix, FcitxKey_a, false, "first key types into preedit") || !expectSend(fix, FcitxKey_s, true, "trigger key is swallowed by the replacement"))
        return 1;
    fix.joinHelper();

    // N=2: the job paces 2 backspace taps synchronously at push time.
    if (!expectTaps(2, "first job taps", "the initial Telex replacement did not pace 2 backspace taps"))
        return 1;
    // N=2: sleeps are [pre=0 (no prior commit), max(17,33)=33 before last tap].
    {
        auto sleeps = sleepMillis(fcitx::KeyPacer::instance().lastPushedJobForTest());
        check(sleeps.size() == 2 && sleeps[0] == 0 && sleeps[1] == 33, "default delays on the first job", "want sleeps=[0,33], got size=" + std::to_string(sleeps.size()));
        if (failures > 0)
            return 1;
    }

    if (!expectSend(fix, FcitxKey_x, true, "buffered key is swallowed") || !fix.context->commits().empty()) {
        check(false, "buffer key x before deletion completes", fix.commitList());
        return 1;
    }
    // Echoed backspaces on timers: the first passes through so the app can
    // delete, the last is swallowed and lifts the barrier — the commit lands
    // and the buffered x replays into a fresh job synchronously inside the
    // callback, where its cooldown sleep is asserted.
    fix.at(std::chrono::milliseconds{300}, [&fix] { expectSend(fix, FcitxKey_BackSpace, false, "first echoed backspace passes through"); });
    fix.at(std::chrono::milliseconds{600}, [&fix] {
        if (!expectSend(fix, FcitxKey_BackSpace, true, "last echoed backspace is swallowed"))
            return;
        if (!expectTaps(2, "replay job taps", "buffered x replay did not pace 2 backspace taps"))
            return;
        // Replay happens right after the previous commit: pre ~= 1000ms cooldown.
        auto sleeps = sleepMillis(fcitx::KeyPacer::instance().lastPushedJobForTest());
        check(!sleeps.empty() && sleeps[0] >= 700 && sleeps[0] <= 1000, "commit cooldown on the repeated job",
              "want pre in [700,1000], got " + (sleeps.empty() ? std::string("no sleeps") : "pre=" + std::to_string(sleeps[0])));
    });
    fix.at(std::chrono::milliseconds{900}, [&fix] { expectSend(fix, FcitxKey_BackSpace, false, "replay first echo passes through"); });
    fix.at(std::chrono::milliseconds{1200}, [&fix] { expectSend(fix, FcitxKey_BackSpace, true, "replay last echo is swallowed"); });
    fix.quitAt(std::chrono::milliseconds{3000});
    fix.testInstance.instance.eventLoop().exec();

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "replacement-delays: all checks passed\n";
    return 0;
}
