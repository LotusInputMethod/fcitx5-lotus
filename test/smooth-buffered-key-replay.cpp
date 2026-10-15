// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file smooth-buffered-key-replay.cpp
 * @brief A key typed while a Smooth replacement is in flight is buffered and
 *        replayed after the commit, producing its own replacement.
 *
 * Runs KeyPacer in immediate mode against a mini EIS server: job shapes are
 * inspected via lastPushedJobForTest, while echoed backspaces are re-sent
 * through the engine on loop timers — accepted means swallowed, forwarded
 * means passed through.
 */

#include "mini-eis-fixture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

    const std::string kPath("lotus-test-smooth-replay-kb");

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

    // Counts backspace taps in the most recently pushed job.
    bool jobTaps(size_t& taps, std::string& detail) {
        return lotus_test::countJobTaps(fcitx::KeyPacer::instance().lastPushedJobForTest(), KEY_BACKSPACE, taps, detail);
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-smooth-buffered-key-replay");
    lotus_test::EisFixture fix;
    fix.engine = std::make_unique<fcitx::LotusEngine>(&fix.testInstance.instance);
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    fix.engine->setConfig(config);
    if (fix.engine->config().mode.value() != fcitx::LotusMode::Smooth || fix.engine->config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        check(false, "configure Smooth/Telex", "configured mode or input method differs");
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

    // Telex a, s changes the real Bamboo preedit a -> á. Smooth mode replaces
    // the old character through the pacer job.
    if (!expectSend(fix, FcitxKey_a, false, "first key types into preedit") || !expectSend(fix, FcitxKey_s, true, "trigger key is swallowed by the replacement"))
        return 1;
    fix.joinHelper();

    size_t      backspaces = 0;
    std::string detail;
    if (!jobTaps(backspaces, detail) || backspaces == 0) {
        check(false, "first job taps", "the initial Telex replacement did not pace backspace taps -- " + detail);
        return 1;
    }

    if (!expectSend(fix, FcitxKey_x, true, "buffered key is swallowed") || !fix.context->commits().empty()) {
        check(false, "buffer key x before deletion completes", fix.commitList());
        return 1;
    }
    // First echo round: every press but the last passes through; the last is
    // swallowed, the commit lands and the buffered x replays into a fresh job
    // synchronously inside the callback.
    for (size_t i = 0; i < backspaces; ++i) {
        const bool last = (i + 1 == backspaces);
        if (!last) {
            fix.at(std::chrono::milliseconds{300 * (i + 1)}, [&fix] { expectSend(fix, FcitxKey_BackSpace, false, "echoed backspace passes through"); });
            continue;
        }
        fix.at(std::chrono::milliseconds{300 * (i + 1)}, [&fix, backspaces] {
            if (!expectSend(fix, FcitxKey_BackSpace, true, "last echoed backspace is swallowed"))
                return;
            size_t      replayBackspaces = 0;
            std::string replayDetail;
            if (!jobTaps(replayBackspaces, replayDetail) || replayBackspaces == 0) {
                check(false, "replay job taps", "buffered x replay did not pace backspace taps -- " + replayDetail);
                return;
            }
            for (size_t j = 0; j < replayBackspaces; ++j) {
                const bool replayLast = (j + 1 == replayBackspaces);
                // Relative offsets: this callback runs at the last first-round
                // echo, so +300 lands 300ms later, not at an absolute time.
                fix.at(std::chrono::milliseconds{300 * (j + 1)}, [&fix, replayLast] {
                    if (!expectSend(fix, FcitxKey_BackSpace, replayLast, "replay echo"))
                        return;
                    if (replayLast) {
                        const std::vector<std::string> expected{"á", "ã"};
                        check(fix.context->commits() == expected, "verify final replay commits", fix.commitList());
                    }
                });
            }
            fix.quitAt(std::chrono::milliseconds{300 * replayBackspaces + 500});
        });
    }
    // Backstop: every quit above sits inside a callback that may return early
    // on failure. Without this a single failed echo hangs until TIMEOUT.
    fix.quitAt(std::chrono::milliseconds{15000});
    fix.testInstance.instance.eventLoop().exec();

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "smooth-buffered-key-replay: all checks passed\n";
    return 0;
}
