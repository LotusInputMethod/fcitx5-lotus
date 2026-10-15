// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file select-mode-replacement.cpp
 * @brief Select mode: a replacement is delivered as a Shift+Left selection
 *        stream. The engine commits the replacement once all echoed arrows
 *        have arrived, the intermediate arrows pass through so the app can
 *        extend the selection, and no arrow is replayed afterwards.
 *
 * Runs KeyPacer in immediate mode against a mini EIS server: the selection
 * job shape is inspected via lastPushedJobForTest, while echoed arrows are
 * re-sent through the engine on loop timers — accepted means swallowed,
 * forwarded means passed through.
 */

#include "mini-eis-fixture.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace {

    const std::string kPath("lotus-test-select-replacement-kb");

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

    // Parses the most recently pushed job as a selection stream; returns the
    // Left-tap count (selects).
    bool jobSelects(size_t& selects, std::string& detail) {
        return lotus_test::parseSelectJob(fcitx::KeyPacer::instance().lastPushedJobForTest(), selects, detail);
    }

    // Schedules one echo round: the first selects-1 Lefts pass through so the
    // app extends the selection, the last Left is swallowed, and the Delete
    // lifts the barrier (E+1th echo) so the commit lands — forwarded so the
    // app deletes before the commit. Runs `done` inside the Delete callback.
    // All offsets are relative to the call time (see EisFixture::at).
    void echoRound(lotus_test::EisFixture& fix, size_t selects, std::function<void()> done) {
        for (size_t i = 0; i + 1 < selects; ++i) {
            fix.at(std::chrono::milliseconds{300 * (i + 1)}, [&fix] { expectSend(fix, FcitxKey_Left, false, "echoed arrow passes through"); });
        }
        fix.at(std::chrono::milliseconds{300 * selects}, [&fix] { expectSend(fix, FcitxKey_Left, true, "last echoed arrow is swallowed"); });
        fix.at(std::chrono::milliseconds{300 * (selects + 1)}, [&fix, done = std::move(done)] {
            if (!expectSend(fix, FcitxKey_Delete, false, "echoed Delete deletes before the commit"))
                return;
            done();
        });
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-mode-replacement");
    lotus_test::EisFixture fix;
    fix.engine = std::make_unique<fcitx::LotusEngine>(&fix.testInstance.instance);
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    fix.engine->setConfig(config);
    if (fix.engine->config().mode.value() != fcitx::LotusMode::Select || fix.engine->config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        check(false, "configure Select/Telex", "configured mode or input method differs");
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

    // Telex a, s changes the Bamboo preedit a -> á. Select mode must ask the
    // pacer to select the old character(s) instead of sending backspaces.
    if (!expectSend(fix, FcitxKey_a, false, "first key types into preedit") || !expectSend(fix, FcitxKey_s, true, "trigger key is swallowed by the replacement"))
        return 1;
    fix.joinHelper();

    size_t      selects = 0;
    std::string detail;
    if (!jobSelects(selects, detail) || selects == 0) {
        check(false, "first selection stream", "the initial Telex replacement did not pace a selection -- " + detail);
        return 1;
    }

    // A key typed while the selection is in flight must not commit immediately.
    if (!expectSend(fix, FcitxKey_x, true, "buffered key is swallowed") || !fix.context->commits().empty()) {
        check(false, "buffer key x before the selection completes", fix.commitList());
        return 1;
    }

    // First echo round: arrows pass through, the last one is swallowed, the
    // Delete lifts the barrier so á commits — and the buffered x replays
    // into a second selection synchronously inside the Delete callback,
    // where the replay round is scheduled.
    echoRound(fix, selects, [&fix, selects] {
        const std::vector<std::string> afterFirst{"á"};
        if (fix.context->commits() != afterFirst) {
            check(false, "verify commit after the last arrow", fix.commitList());
            return;
        }
        size_t      replaySelects = 0;
        std::string replayDetail;
        if (!jobSelects(replaySelects, replayDetail) || replaySelects == 0) {
            check(false, "replay selection stream", "buffered key was not replayed after the selection completed -- " + replayDetail);
            return;
        }
        // No arrow is replayed: the second stream matches a fresh replacement.
        echoRound(fix, replaySelects, [&fix] {
            const std::vector<std::string> expected{"á", "ã"};
            check(fix.context->commits() == expected, "verify final replay commits", fix.commitList());
        });
        fix.quitAt(std::chrono::milliseconds{300 * (replaySelects + 1) + 500});
    });
    // Backstop: every quit above sits inside a callback that may return early
    // on failure. Without this a single failed echo hangs until TIMEOUT.
    fix.quitAt(std::chrono::milliseconds{15000});
    fix.testInstance.instance.eventLoop().exec();

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "select-mode-replacement: all checks passed\n";
    return 0;
}
