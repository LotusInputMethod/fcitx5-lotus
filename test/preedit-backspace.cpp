// SPDX-License-Identifier: GPL-3.0-or-later
//
// Backspace inside a Preedit composition must edit the composition, not commit it: the engine is
// addressed by keysym, so the addon must hand over FcitxKey_BackSpace, never the Unicode 0x08 the
// keysym maps to (0x08 is an unknown key for the engine, which then commits the whole preedit).
#include "engine-fixture.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int main() {
    configureTestPaths("fcitx5-lotus-preedit-backspace");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // "lo" stays a composition.
    if (!sendKey(engine, entry, *context, FcitxKey_l, true, "composition keys must be swallowed into the preedit") ||
        !sendKey(engine, entry, *context, FcitxKey_o, true, "composition keys must be swallowed into the preedit"))
        return 1;
    if (context->inputPanel().clientPreedit().toString() != "lo") {
        reportFailure("compose", "client preedit=lo", "client preedit=" + context->inputPanel().clientPreedit().toString(), "the test cannot compose");
        return 1;
    }

    // Backspace removes the last rune, is swallowed, and commits nothing.
    if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, true, "backspace must edit the composition, not commit it"))
        return 1;
    if (!context->commits().empty()) {
        reportFailure("backspace inside a composition", "no commits", commitList(*context), "backspace committed the remaining preedit instead of editing it");
        return 1;
    }
    if (context->inputPanel().clientPreedit().toString() != "l") {
        reportFailure("backspace edits the composition", "client preedit=l", "client preedit=" + context->inputPanel().clientPreedit().toString(),
                      "backspace must shorten the composition");
        return 1;
    }

    // Backspace on the last rune ends the composition without committing text.
    if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, true, "backspace must edit the composition, not commit it"))
        return 1;
    if (!context->commits().empty() || !context->inputPanel().clientPreedit().toString().empty()) {
        reportFailure("backspace on the last rune", "no commits, empty preedit", "commits=" + std::to_string(context->commits().size()),
                      "backspace on the last rune must only end the composition");
        return 1;
    }

    // With nothing composed, Backspace belongs to the application.
    if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "backspace must edit the composition, not commit it"))
        return 1;

    std::printf("lotus preedit backspace: all checks passed\n");
    return 0;
}
