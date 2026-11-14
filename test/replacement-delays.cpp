// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file replacement-delays.cpp
 * @brief Headless regression test: the global default delays reach the uinput
 *        server through KbMsg (interval = DefaultBackspaceInterval,
 *        postDelay = DefaultPostDelay) and DefaultCommitInterval delays a
 *        replacement that follows a recent commit.
 */

#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <memory>
#include <string>

namespace {

    bool receiveBackspaceRequest(KbSocketListener& listener, KbMsg& msg, const char* meaning, const char* timeoutExpected = "request within 5000 ms") {
        if (!listener.receive(msg, meaning, timeoutExpected))
            return false;
        if (msg.op != KbOp::Backspace || msg.count <= 0) {
            reportFailure("receive replacement request", "op=backspace, count > 0", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                          meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-replacement-delays");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DefaultBackspaceInterval", "17");
    config.setValueByPath("DefaultPostDelay", "33");
    config.setValueByPath("DefaultCommitInterval", "1000");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || *engine.config().defaultBackspaceInterval != 17 || *engine.config().defaultPostDelay != 33 ||
        *engine.config().defaultCommitInterval != 1000) {
        reportFailure("configure Smooth/Telex and default delays", "mode=Smooth, intervals 17/33/1000", "configured values differ",
                      "the delay wiring cannot be exercised with the wrong configuration");
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

    // Telex a, s changes the real Bamboo preedit a -> á; Smooth mode replaces
    // the old character through the kbSocket transport.
    if (!sendKey(engine, entry, *context, FcitxKey_a, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_s, true, "the trigger key must be swallowed to start the replacement"))
        return 1;

    KbMsg first{};
    if (!receiveBackspaceRequest(listener, first, "the initial Telex replacement request did not arrive"))
        return 1;
    if (first.interval != 17 || first.postDelay != 33 || first.preDelay != 0) {
        reportFailure("default delays on the first request", "interval=17, postDelay=33, preDelay=0",
                      "interval=" + std::to_string(first.interval) + ", postDelay=" + std::to_string(first.postDelay) + ", preDelay=" + std::to_string(first.preDelay),
                      "DefaultBackspaceInterval/DefaultPostDelay must be forwarded and no commit happened before");
        return 1;
    }

    if (!sendKey(engine, entry, *context, FcitxKey_x, true, "the buffered key must be swallowed while the replacement runs") || !context->commits().empty()) {
        reportFailure("buffer key x before deletion completes", "no immediate commits", "commits=" + std::to_string(context->commits().size()),
                      "buffered key x was emitted before the first replacement completed");
        return 1;
    }
    for (size_t i = 0; i < first.count; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the burst backspace must echo to the app so it deletes the old word"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement"))
        return 1;

    KbMsg second{};
    if (!receiveBackspaceRequest(listener, second, "buffered key was not replayed after deletion", "buffered x replay starts another replacement request within 5000 ms"))
        return 1;
    if (second.preDelay < 900 || second.preDelay > 1000) {
        reportFailure("commit cooldown on the repeated request", "preDelay in [900, 1000]", "preDelay=" + std::to_string(second.preDelay),
                      "DefaultCommitInterval=1000 must delay a replacement that follows the previous commit");
        return 1;
    }
    for (size_t i = 0; i < second.count; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the replayed replacement's backspaces must echo to the app"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor of the replayed replacement must be swallowed"))
        return 1;
    return 0;
}
