// SPDX-License-Identifier: GPL-3.0-or-later
//
// Select mode: a replacement is delivered as a Shift+Left selection request on
// the keyboard socket instead of a backspace stream. The engine commits the
// replacement once all echoed left arrows have arrived, the intermediate arrows
// pass through so the app can extend the selection, and no arrow is replayed
// afterwards.
#include "engine-fixture.h"
#include "fcitx-utils/keysym.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

    bool receiveSelectRequest(KbSocketListener& listener, size_t& count, const char* meaning, const char* timeoutExpected = "request within 5000 ms") {
        KbMsg msg{};
        if (!listener.receive(msg, meaning, timeoutExpected))
            return false;
        if (msg.op != KbOp::Select || msg.count <= 0) {
            reportFailure("receive replacement request", "op=select, count > 0", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                          meaning);
            return false;
        }
        count = msg.count;
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-mode-replacement");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Select || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Select/Telex", "mode=Select, input method=Telex", "configured mode or input method differs",
                      "the select test cannot exercise Select mode Telex behavior");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    // Telex a, s changes the Bamboo preedit a -> á. Select mode must ask the
    // uinput server to select the old character(s) instead of sending backspaces.
    if (!sendKey(engine, entry, *context, FcitxKey_a, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_s, true, "the trigger key must be swallowed to start the replacement"))
        return 1;
    size_t selects = 0;
    if (!receiveSelectRequest(listener, selects, "the initial Telex replacement did not request a selection"))
        return 1;

    // A key typed while the selection is in flight must not commit immediately.
    if (!sendKey(engine, entry, *context, FcitxKey_x, true, "the buffered key must be swallowed while the replacement runs") || !context->commits().empty()) {
        reportFailure("buffer key x before the selection completes", "no immediate commits", commitList(*context), "buffered key x was emitted before the selection completed");
        return 1;
    }

    // Echoed left arrows: every press but the last passes through so the app can
    // extend the selection; the last one is swallowed and commits the replacement.
    for (size_t i = 0; i < selects; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_Left, false, "the selection arrows must echo to the app so the app can extend the selection"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Delete, false, "the Delete echo must reach the application"))
        return 1;
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement"))
        return 1;
    const std::vector<std::string> afterFirst{"á"};
    if (context->commits() != afterFirst) {
        reportFailure("verify commit after the last left arrow", "commits=['á']", commitList(*context) + ", selects=" + std::to_string(selects),
                      "the replacement was not committed exactly once when the selection completed");
        return 1;
    }

    // The buffered x is replayed after the commit and starts a second selection.
    // No left arrow is replayed: only one new request arrives and it matches the
    // count the replacement needs.
    size_t replaySelects = 0;
    if (!receiveSelectRequest(listener, replaySelects, "buffered key was not replayed after the selection completed",
                              "buffered x replay starts another selection request within 5000 ms"))
        return 1;
    for (size_t i = 0; i < replaySelects; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_Left, false, "the selection arrows must echo to the app so the app can extend the selection"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Delete, false, "the Delete echo must reach the application"))
        return 1;
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement"))
        return 1;

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        reportFailure("verify final replay commits", "commits=['á']['ã']",
                      commitList(*context) + ", first-selects=" + std::to_string(selects) + ", replay-selects=" + std::to_string(replaySelects),
                      "the buffered key replay did not produce the expected final commits, or a left arrow was replayed");
        return 1;
    }
    return 0;
}
