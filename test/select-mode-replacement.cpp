// SPDX-License-Identifier: GPL-3.0-or-later
//
// Select mode: a replacement is delivered as a Shift+Left selection request on
// the keyboard socket instead of a backspace stream. The engine commits the
// replacement once all echoed left arrows have arrived, the intermediate arrows
// pass through so the app can extend the selection, and no arrow is replayed
// afterwards.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"

#include <memory>
#include <string>
#include <vector>

namespace {

    bool receiveSelectRequest(KbSocketListener& listener, int& count, const char* meaning, const char* timeoutExpected = "request within 5000 ms") {
        KbMsg msg{};
        if (!listener.receive(msg, meaning, timeoutExpected))
            return false;
        if (msg.op != KB_OP_SELECT || msg.count <= 0) {
            reportFailure("receive replacement request", "op=select, count > 0", "op=" + std::to_string(msg.op) + ", count=" + std::to_string(msg.count), meaning);
            return false;
        }
        count = msg.count;
        return true;
    }

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(requireAccepted),
                          "accepted=" + std::to_string(event.accepted()) + ", commits=" + std::to_string(context.commits().size()),
                          "Select echo handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

    std::string commitList(const TestInputContext& context) {
        std::string actual = "commits=";
        for (const auto& commit : context.commits())
            actual += "['" + commit + "']";
        return actual;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-mode-replacement");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Select (Shift+Left)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Select || engine.config().inputMethod.value() != "Telex") {
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
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    int selects = 0;
    if (!receiveSelectRequest(listener, selects, "the initial Telex replacement did not request a selection"))
        return 1;

    // A key typed while the selection is in flight must not commit immediately.
    if (!send(engine, entry, *context, FcitxKey_x, true) || !context->commits().empty()) {
        reportFailure("buffer key x before the selection completes", "no immediate commits", commitList(*context), "buffered key x was emitted before the selection completed");
        return 1;
    }

    // Echoed left arrows: every press but the last passes through so the app can
    // extend the selection; the last one is swallowed and commits the replacement.
    for (int i = 0; i < selects; ++i) {
        if (!send(engine, entry, *context, FcitxKey_Left, i + 1 == selects))
            return 1;
    }
    const std::vector<std::string> afterFirst{"á"};
    if (context->commits() != afterFirst) {
        reportFailure("verify commit after the last left arrow", "commits=['á']", commitList(*context) + ", selects=" + std::to_string(selects),
                      "the replacement was not committed exactly once when the selection completed");
        return 1;
    }

    // The buffered x is replayed after the commit and starts a second selection.
    // No left arrow is replayed: only one new request arrives and it matches the
    // count the replacement needs.
    int replaySelects = 0;
    if (!receiveSelectRequest(listener, replaySelects, "buffered key was not replayed after the selection completed",
                              "buffered x replay starts another selection request within 5000 ms"))
        return 1;
    for (int i = 0; i < replaySelects; ++i) {
        if (!send(engine, entry, *context, FcitxKey_Left, i + 1 == replaySelects))
            return 1;
    }

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        reportFailure("verify final replay commits", "commits=['á']['ã']",
                      commitList(*context) + ", first-selects=" + std::to_string(selects) + ", replay-selects=" + std::to_string(replaySelects),
                      "the buffered key replay did not produce the expected final commits, or a left arrow was replayed");
        return 1;
    }
    return 0;
}
