// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Auto capitalize after sentence-ending punctuation (. ! ? Enter)" must survive the client reset
// every GTK frontend sends when Enter inserts a newline: the user keeps typing in the same input
// context, so the sentence state cannot be dropped there. A focus-out is a real context switch and
// must still clear it.
#include "lotus-engine.h"
#include "test-input-context.h"
#include "test-util.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace {

    // Returns the key symbol the engine left on the event: auto-capitalize rewrites it in place
    // (keyEvent.setKey) before the key is handled or forwarded.
    fcitx::KeySym sendKey(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        return event.key().sym();
    }

    // Runs one reset event, the way a frontend reports it to the engine.
    void sendReset(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::EventType type) {
        fcitx::InputContextEvent event(&context, type);
        engine.reset(entry, event);
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-auto-capitalize-reset");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("AutoCapitalizeAfterPunctuation", "True");
    engine.setConfig(config);
    if (!engine.config().autoCapitalizeAfterPunctuation.value()) {
        reportFailure("enable AutoCapitalizeAfterPunctuation", "option is on", "option is off", "the test cannot exercise sentence capitalization");
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // Control: without a finished sentence the letter stays lowercase.
    const fcitx::KeySym plainLetter = sendKey(engine, entry, *context, FcitxKey_t);
    if (plainLetter != FcitxKey_t) {
        reportFailure("plain letter", "t", fcitx::Key(plainLetter).toString(), "nothing must capitalize before a sentence ends");
        return 1;
    }

    // Phase A: a client reset (what GTK does on Enter) must keep the sentence state.
    sendKey(engine, entry, *context, FcitxKey_Return);
    sendReset(engine, entry, *context, fcitx::EventType::InputContextReset);
    const fcitx::KeySym afterClientReset = sendKey(engine, entry, *context, FcitxKey_t);
    if (afterClientReset != FcitxKey_T) {
        reportFailure("sentence state survives a client reset", "T (the next character starts a sentence)", fcitx::Key(afterClientReset).toString(),
                      "a client reset must not drop the Enter that ended the previous sentence");
        return 1;
    }

    // Phase B: a focus-out is a real context switch and must clear the sentence state. uinput modes
    // defer that reset to the next activation, so the next context activates after the bounce window.
    sendKey(engine, entry, *context, FcitxKey_Return);
    fcitx::InputContextEvent out(context.get(), fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, out);
    engine.reset(entry, out);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    context->focusIn();
    fcitx::InputContextEvent focusIn(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focusIn);
    const fcitx::KeySym afterFocusOut = sendKey(engine, entry, *context, FcitxKey_t);
    if (afterFocusOut != FcitxKey_t) {
        reportFailure("sentence state cleared on focus out", "t (lowercase)", fcitx::Key(afterFocusOut).toString(),
                      "focus out must not leave a pending capitalization for the next context");
        return 1;
    }

    std::printf("lotus auto capitalize reset: all checks passed\n");
    return 0;
}
