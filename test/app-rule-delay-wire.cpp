// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app-rule-delay-wire.cpp
 * @brief Headless regression test: a per-app rule's backspace interval and post
 *        delay override the global defaults in the pacer job.
 *
 * Runs KeyPacer in immediate mode against a mini EIS server: the job shape is
 * inspected via lastPushedJobForTest (no wall-clock involved), while echoed
 * backspaces are re-sent through the engine on loop timers.
 */

#include "mini-eis-fixture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

    const std::string kPath("lotus-test-app-rule-delay-wire-kb");

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

} // namespace

int main() {
    const char* testName = "fcitx5-lotus-app-rule-delay-wire";
    configureTestPaths(testName);

    // The mock input context reports the program name "test"; its rule sets the
    // backspace interval to 20 ms and the post delay to 25 ms.
    const auto rulesFile = std::filesystem::temp_directory_path() / testName / "config/fcitx5/conf/lotus-app-rules.conf";
    {
        std::filesystem::create_directories(rulesFile.parent_path());
        std::ofstream file(rulesFile);
        file << "test=1,0,20,25\n";
    }

    lotus_test::EisFixture fix;
    fix.engine = std::make_unique<fcitx::LotusEngine>(&fix.testInstance.instance);
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DefaultBackspaceInterval", "17");
    config.setValueByPath("DefaultPostDelay", "33");
    fix.engine->setConfig(config);
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

    if (!expectSend(fix, FcitxKey_a, false, "first key types into preedit") || !expectSend(fix, FcitxKey_s, true, "trigger key is swallowed by the replacement"))
        return 1;
    fix.joinHelper();

    // The job paces 2 backspace taps: 4 KeyMsgs, no more, no less.
    {
        size_t      taps = 0;
        std::string detail;
        const bool  ok = lotus_test::countJobTaps(fcitx::KeyPacer::instance().lastPushedJobForTest(), KEY_BACKSPACE, taps, detail);
        check(ok && taps == 2, "tap stream size", "want 4 KeyMsgs (2 taps), " + detail);
        if (failures > 0)
            return 1;
    }
    // N=2 with rule interval=20/post=25: sleeps are [pre=0, max(20,25)=25].
    // Exact 25 (not the 33 default) proves the per-app rule wins.
    {
        std::vector<int64_t> sleeps;
        for (const auto& item : fcitx::KeyPacer::instance().lastPushedJobForTest()) {
            if (const auto* sleep = std::get_if<fcitx::SleepFor>(&item))
                sleeps.push_back(sleep->delay.count());
        }
        check(sleeps.size() == 2 && sleeps[0] == 0 && sleeps[1] == 25, "per-app delays on the job",
              "want sleeps=[0,25], got size=" + std::to_string(sleeps.size()) + (sleeps.empty() ? "" : " last=" + std::to_string(sleeps.back())));
        if (failures > 0)
            return 1;
    }
    fix.at(std::chrono::milliseconds{300}, [&fix] { expectSend(fix, FcitxKey_BackSpace, false, "first echoed backspace passes through"); });
    fix.at(std::chrono::milliseconds{600}, [&fix] { expectSend(fix, FcitxKey_BackSpace, true, "last echoed backspace is swallowed"); });
    fix.quitAt(std::chrono::milliseconds{3000});
    fix.testInstance.instance.eventLoop().exec();

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "app-rule-delay-wire: all checks passed\n";
    return 0;
}
