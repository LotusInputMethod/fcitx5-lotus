// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression: shortcut keys carrying modifiers (Ctrl/Alt/Super) are forwarded
// without input, so they must neither consume a pending capitalization nor arm
// sentence state. Without the modifier guard in applyAutoCapitalize():
//   - "Hello. " + Ctrl+C + "world" degrades to "Hello. world" (state consumed,
//     shortcut keysym even rewritten to Ctrl+C);
//   - Ctrl+. arms isPrevPunctuation_, so a later Space capitalizes wrongly.
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

    struct Fixture {
        TestInstance                      testInstance;
        fcitx::LotusEngine                engine;
        std::unique_ptr<TestInputContext> context;
        fcitx::InputMethodEntry           entry;
        Fixture() : engine(&testInstance.instance), entry("lotus", "Lotus", "vi", "lotus") {
            fcitx::RawConfig config;
            config.setValueByPath("Mode", "Uinput (Smooth)");
            config.setValueByPath("InputMethod", "Telex");
            config.setValueByPath("AutoCapitalizeAfterPunctuation", "True");
            engine.setConfig(config);
            context = std::make_unique<TestInputContext>(&testInstance.instance);
            context->focusIn();
            fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
            engine.activate(entry, focus);
        }
    };

    // Sends a key through the engine, returns the symbol left on the event:
    // auto-capitalize rewrites it in place (keyEvent.setKey) before handling.
    fcitx::KeySym sendKey(Fixture& f, fcitx::KeySym sym, fcitx::KeyStates states = fcitx::KeyStates()) {
        fcitx::KeyEvent event(f.context.get(), fcitx::Key(sym, states), false);
        f.engine.keyEvent(f.entry, event);
        return event.key().sym();
    }

    void reportFailure(const char* step, const std::string& expected, const std::string& actual, const char* meaning) {
        std::printf("FAIL %s\nExpected: %s\nActual: %s\nMeaning: %s\n", step, expected.c_str(), actual.c_str(), meaning);
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-auto-capitalize-shortcut");
    int failures = 0;

    // Scenario 1: a pending capitalization survives a shortcut.
    {
        Fixture f;
        if (!f.engine.config().autoCapitalizeAfterPunctuation.value()) {
            reportFailure("enable AutoCapitalizeAfterPunctuation", "option is on", "option is off", "the test cannot exercise sentence capitalization");
            return 1;
        }
        sendKey(f, FcitxKey_period);
        sendKey(f, FcitxKey_space);

        // Fcitx normalizes Ctrl+c to Ctrl+C at the event layer; what matters
        // here is that the shortcut leaves the pending capitalization alone.
        sendKey(f, FcitxKey_c, fcitx::KeyState::Ctrl);

        const fcitx::KeySym next = sendKey(f, FcitxKey_w);
        if (next != FcitxKey_W) {
            reportFailure("capital survives Ctrl+C", "W", fcitx::Key(next).toString(), "Ctrl+C consumed the pending sentence capitalization");
            ++failures;
        }
    }

    // Scenario 2: a shortcut punctuation never ends a sentence.
    {
        Fixture f;
        sendKey(f, FcitxKey_period, fcitx::KeyState::Ctrl);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_a);
        if (next != FcitxKey_a) {
            reportFailure("Ctrl+. does not end a sentence", "a", fcitx::Key(next).toString(),
                          "Ctrl+. is a forwarded shortcut, not typed punctuation, so Space must not arm capitalization");
            ++failures;
        }
    }

    // Scenario 3: a shortcut must not clear a pending punctuation state.
    {
        Fixture f;
        sendKey(f, FcitxKey_period);
        sendKey(f, FcitxKey_c, fcitx::KeyState::Ctrl);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_w);
        if (next != FcitxKey_W) {
            reportFailure("punctuation survives Ctrl+C", "W", fcitx::Key(next).toString(), "Ctrl+C cleared isPrevPunctuation_ before Space could arm capitalization");
            ++failures;
        }
    }

    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("lotus auto capitalize shortcut: all checks passed\n");
    return 0;
}
