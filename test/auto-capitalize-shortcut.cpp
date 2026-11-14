// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression (section-2): shortcut punctuation (e.g. Ctrl+.) is forwarded
// without input, so it must never arm sentence state in
// LotusState::applyAutoCapitalize (guard: key().hasModifier() over SimpleMask,
// which includes Shift; normalize strips Shift only for unicode/letter syms).
// Without the guard: Ctrl+. arms isPrevPunctuation_ (later Space mis-capitalizes),
// and Ctrl+C clears a pending isPrevPunctuation_ before Space can arm it.
#include "lotus-engine.h"
#include "test-input-context.h"
#include "test-util.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

    struct Fixture {
        TestInstance&                     testInstance;
        fcitx::LotusEngine                engine;
        std::unique_ptr<TestInputContext> context;
        fcitx::InputMethodEntry           entry;

        explicit Fixture(TestInstance& instance) : testInstance(instance), engine(&instance.instance), entry("lotus", "Lotus", "vi", "lotus") {
            fcitx::RawConfig config;
            config.setValueByPath("Mode", "Uinput (Smooth)");
            config.setValueByPath("InputMethod", "Telex");
            config.setValueByPath("AutoCapitalizeAfterPunctuation", "True");
            engine.setConfig(config);
        }

        /// Fresh input context: every case starts with an empty sentence state, and one instance serves them all.
        void newSession() {
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

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-auto-capitalize-shortcut");
    TestInstance testInstance;

    // (a) Shortcut punctuation never ends a sentence.
    {
        Fixture f(testInstance);
        f.newSession();
        if (!f.engine.config().autoCapitalizeAfterPunctuation.value()) {
            reportFailure("enable AutoCapitalizeAfterPunctuation", "option is on", "option is off", "the test cannot exercise sentence capitalization");
            return 1;
        }
        sendKey(f, FcitxKey_period, fcitx::KeyState::Ctrl);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_a);
        if (next != FcitxKey_a) {
            reportFailure("Ctrl+. does not end a sentence", "a", fcitx::Key(next).toString(),
                          "Ctrl+. is a forwarded shortcut, not typed punctuation, so Space must not arm capitalization");
            return 1;
        }
    }

    // (b) Control: real punctuation still ends a sentence (no regression).
    {
        Fixture f(testInstance);
        f.newSession();
        sendKey(f, FcitxKey_period);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_w);
        if (next != FcitxKey_W) {
            reportFailure("real punctuation still capitalizes", "W", fcitx::Key(next).toString(),
                          "bare period + Space must keep arming capitalization after the modifier guard lands");
            return 1;
        }
    }

    // (c) Pending punctuation survives a shortcut before Space arms it.
    {
        Fixture f(testInstance);
        f.newSession();
        sendKey(f, FcitxKey_period);
        // Fcitx normalizes Ctrl+c to Ctrl+C at the event layer; what matters
        // here is that the shortcut leaves the pending punctuation alone.
        sendKey(f, FcitxKey_c, fcitx::KeyState::Ctrl);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_w);
        if (next != FcitxKey_W) {
            reportFailure("punctuation survives Ctrl+C", "W", fcitx::Key(next).toString(), "Ctrl+C cleared isPrevPunctuation_ before Space could arm capitalization");
            return 1;
        }
    }

    // (d) Alt variant of (a): Alt+. never ends a sentence either.
    {
        Fixture f(testInstance);
        f.newSession();
        sendKey(f, FcitxKey_period, fcitx::KeyState::Alt);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_a);
        if (next != FcitxKey_a) {
            reportFailure("Alt+. does not end a sentence", "a", fcitx::Key(next).toString(),
                          "Alt+. is a forwarded shortcut, not typed punctuation, so Space must not arm capitalization");
            return 1;
        }
    }

    // (e) Shift+? still ends a sentence: normalize strips Shift for unicode syms,
    // so the guard sees it as typed punctuation, not a shortcut.
    {
        Fixture f(testInstance);
        f.newSession();
        sendKey(f, FcitxKey_question, fcitx::KeyState::Shift);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_w);
        if (next != FcitxKey_W) {
            reportFailure("Shift+? still ends a sentence", "W", fcitx::Key(next).toString(),
                          "Shift is stripped from unicode syms at normalize, so Shift+? is typed punctuation and Space must arm capitalization");
            return 1;
        }
    }

    // (f) Forwarded shortcuts never mutate sentence state: Ctrl+A after a
    // pending period must leave it alone. Known limitation: content effects of
    // shortcuts are unobservable at the key-event layer (a shortcut may have
    // deleted the punctuation), so preserve is the rule.
    {
        Fixture f(testInstance);
        f.newSession();
        sendKey(f, FcitxKey_period);
        sendKey(f, FcitxKey_a, fcitx::KeyState::Ctrl);
        sendKey(f, FcitxKey_space);
        const fcitx::KeySym next = sendKey(f, FcitxKey_w);
        if (next != FcitxKey_W) {
            reportFailure("punctuation survives Ctrl+A", "W", fcitx::Key(next).toString(),
                          "forwarded shortcuts must not clear pending isPrevPunctuation_ before Space arms capitalization");
            return 1;
        }
    }

    std::printf("lotus auto capitalize shortcut: all checks passed\n");
    return 0;
}
