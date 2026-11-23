// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smooth targets clients with autocomplete: the addon commits the anchor U+00B7 right before the
// burst and deletes one extra character. Super Smooth targets clients without autocomplete and uses
// the plain backspace burst.
#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

    bool receiveReplacementRequest(KbSocketListener& listener, KbMsg& msg, size_t expectedCount, const char* meaning) {
        if (!listener.receive(msg, meaning)) {
            return false;
        }
        if (msg.op != KbOp::Backspace || msg.count != expectedCount) {
            reportFailure("receive replacement request", "op=backspace, count=" + std::to_string(expectedCount),
                          "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count), meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-mode-dot-anchor");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    KbSocketListener   listener;
    if (!listener.valid()) {
        return 1;
    }

    // "a" then "s" in Telex composes "á", so the engine replaces the single character.
    const auto runMode = [&](const char* mode, bool expectAnchor) {
        const std::string modeName = expectAnchor ? "Smooth" : "SuperSmooth";
        fcitx::RawConfig  config;
        config.setValueByPath("Mode", mode);
        config.setValueByPath("InputMethod", "Telex");
        engine.setConfig(config);
        if (engine.config().mode.value() != (expectAnchor ? fcitx::LotusMode::Smooth : fcitx::LotusMode::SuperSmooth) ||
            engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
            reportFailure("configure " + modeName + "/Telex", "mode=" + modeName + ", input method=Telex", "configured mode or input method differs",
                          "the test cannot exercise " + modeName + " Telex behavior");
            return false;
        }

        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        context->focusIn();
        fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);
        context->resetPreeditUpdateCount();

        if (!sendKey(engine, entry, *context, FcitxKey_a, false, "plain letters before the trigger must echo to the app") ||
            !sendKey(engine, entry, *context, FcitxKey_s, true, "the trigger key must be swallowed to start the replacement")) {
            return false;
        }

        KbMsg msg{};
        if (!receiveReplacementRequest(listener, msg, expectAnchor ? 2U : 1U, "the Telex replacement request did not arrive")) {
            return false;
        }

        const std::vector<std::string>  anchorOnly{"\u00B7"};
        const std::vector<std::string>  noCommits{};
        const std::vector<std::string>& expectedStart = expectAnchor ? anchorOnly : noCommits;
        if (context->commits() != expectedStart) {
            reportFailure("anchor commit", expectAnchor ? "commits=['·']" : "no commit before the burst", commitList(*context),
                          "Smooth mode commits the dot anchor before the burst and Super Smooth must not");
            return false;
        }

        for (size_t i = 0; i < msg.count; ++i) {
            if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the burst backspace must echo to the app so it deletes the old word")) {
                return false;
            }
        }
        if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement")) {
            return false;
        }

        const std::vector<std::string>  smoothFinal{"\u00B7", "á"};
        const std::vector<std::string>  superFinal{"á"};
        const std::vector<std::string>& expectedFinal = expectAnchor ? smoothFinal : superFinal;
        if (context->commits() != expectedFinal) {
            reportFailure("commit on the anchor", expectAnchor ? "commits=['·']['á']" : "commits=['á']", commitList(*context),
                          "the anchor echo must finish the replacement with the composed word");
            return false;
        }
        return true;
    };

    if (!runMode("Uinput (Smooth)", true) || !runMode("Uinput (Super Smooth)", false)) {
        return 1;
    }
    std::printf("lotus mode dot anchor: all checks passed\n");
    return 0;
}
