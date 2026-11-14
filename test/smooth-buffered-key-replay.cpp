// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

    bool receiveBackspaceRequest(KbSocketListener& listener, size_t& count, const char* meaning, const char* timeoutExpected = "request within 5000 ms") {
        KbMsg msg{};
        if (!listener.receive(msg, meaning, timeoutExpected))
            return false;
        if (msg.op != KbOp::Backspace || msg.count <= 0) {
            reportFailure("receive replacement request", "op=backspace, count > 0", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                          meaning);
            return false;
        }
        count = msg.count;
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-smooth-buffered-key-replay");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs",
                      "the replay test cannot exercise Smooth Telex behavior");
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

    // Telex a, s changes the real Bamboo preedit a -> á. Smooth mode replaces
    // the old character through the kbSocket transport.
    if (!sendKey(engine, entry, *context, FcitxKey_a, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_s, true, "the trigger key must be swallowed to start the replacement"))
        return 1;
    size_t backspaces = 0;
    if (!receiveBackspaceRequest(listener, backspaces, "the initial Telex replacement request did not arrive"))
        return 1;

    if (!sendKey(engine, entry, *context, FcitxKey_x, true, "the buffered key must be swallowed while the replacement runs") || !context->commits().empty()) {
        reportFailure("buffer key x before deletion completes", "no immediate commits", "commits=" + std::to_string(context->commits().size()),
                      "buffered key x was emitted before the first replacement completed");
        return 1;
    }
    for (size_t i = 0; i < backspaces; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the burst backspace must echo to the app so it deletes the old word"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement"))
        return 1;

    size_t replayBackspaces = 0;
    if (!receiveBackspaceRequest(listener, replayBackspaces, "buffered key was not replayed after deletion", "buffered x replay starts another replacement request within 5000 ms"))
        return 1;
    for (size_t i = 0; i < replayBackspaces; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the replayed replacement's backspaces must echo to the app"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor of the replayed replacement must be swallowed"))
        return 1;

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        const auto meaning = "the buffered key replay did not produce the expected final commits; "
                             "first-backspaces=" +
            std::to_string(backspaces) + ", replay-backspaces=" + std::to_string(replayBackspaces);
        reportFailure("verify final replay commits", "commits=['á']['ã']", commitList(*context), meaning);
        return 1;
    }
    return 0;
}
