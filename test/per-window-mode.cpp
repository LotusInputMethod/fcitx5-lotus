// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file per-window-mode.cpp
 * @brief Headless regression test: each window remembers the input mode last
 *        chosen in it, independently of other windows and of the per-app rule.
 *
 * Two input contexts share the program name "test" so they also share the same
 * per-app rule.  Window A is cycled to Emoji, window B to Off.  Without window
 * memory, focusing A again would resolve the app rule (Off); with memory it
 * must stay on Emoji while B stays on Off.  Disabling RememberModePerWindow
 * makes both windows follow the app rule again.
 */

#include "engine-fixture.h"
#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"
#include "test-utils.h"

#include <fcitx-utils/keysym.h>

#include <memory>
#include <string>
#include <string_view>

namespace {

    std::string_view modeName(fcitx::LotusMode mode) {
        switch (mode) {
            case fcitx::LotusMode::Off: return "Off";
            case fcitx::LotusMode::Preedit: return "Preedit";
            case fcitx::LotusMode::Smooth: return "Smooth";
            case fcitx::LotusMode::SuperSmooth: return "SuperSmooth";
            case fcitx::LotusMode::SurroundingText: return "SurroundingText";
            case fcitx::LotusMode::Emoji: return "Emoji";
        }
        return "Unknown";
    }

    bool expectMode(const char* step, fcitx::LotusMode expected, std::string_view meaning) {
        const fcitx::LotusMode actual = ::g_realMode.load();
        if (actual != expected) {
            reportFailure(step, modeName(expected), modeName(actual), meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-per-window-mode");

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("CycleModeKey/0", "F9");
    // Keep the cycle to the text modes this test needs, so no uinput backend
    // connection is attempted by the headless process.
    config.setValueByPath("ShowModeSmooth", "False");
    config.setValueByPath("ShowModeSuperSmooth", "False");
    config.setValueByPath("ShowModeSurroundingText", "False");
    engine.setConfig(config);

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    windowA = std::make_unique<TestInputContext>(&testInstance.instance, "test");
    auto                    windowB = std::make_unique<TestInputContext>(&testInstance.instance, "test");

    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A starts from the global mode", fcitx::LotusMode::Preedit, "a new window must start with the global Mode")) {
        return 1;
    }

    if (!sendKey(engine, entry, *windowA, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window A cycles to Emoji", fcitx::LotusMode::Emoji, "cycling must switch the focused window to the next mode")) {
        return 1;
    }

    loseFocus(engine, entry, *windowA);
    gainFocus(engine, entry, *windowB);
    if (!expectMode("window B inherits the app rule", fcitx::LotusMode::Emoji, "a window without its own choice must follow the per-app rule")) {
        return 1;
    }

    if (!sendKey(engine, entry, *windowB, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window B cycles to Off", fcitx::LotusMode::Off, "cycling must update the per-app rule and window B")) {
        return 1;
    }

    loseFocus(engine, entry, *windowB);
    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A restores its own Emoji mode", fcitx::LotusMode::Emoji, "window A must remember Emoji even though the app rule is Off")) {
        return 1;
    }

    loseFocus(engine, entry, *windowA);
    gainFocus(engine, entry, *windowB);
    if (!expectMode("window B restores its own Off mode", fcitx::LotusMode::Off, "each window must keep its own remembered mode")) {
        return 1;
    }
    loseFocus(engine, entry, *windowB);

    fcitx::RawConfig disabled;
    disabled.setValueByPath("Mode", "Preedit");
    disabled.setValueByPath("InputMethod", "Telex");
    disabled.setValueByPath("CycleModeKey/0", "F9");
    disabled.setValueByPath("RememberModePerWindow", "False");
    disabled.setValueByPath("ShowModeSmooth", "False");
    disabled.setValueByPath("ShowModeSuperSmooth", "False");
    disabled.setValueByPath("ShowModeSurroundingText", "False");
    engine.setConfig(disabled);
    if (*engine.config().rememberModePerWindow) {
        reportFailure("disable per-window memory", "RememberModePerWindow=False", "RememberModePerWindow=True", "the test needs the option disabled");
        return 1;
    }

    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A follows the app rule when memory is disabled", fcitx::LotusMode::Off, "disabling RememberModePerWindow must ignore window A's Emoji memory")) {
        return 1;
    }
    loseFocus(engine, entry, *windowA);

    gainFocus(engine, entry, *windowB);
    if (!expectMode("window B follows the app rule when memory is disabled", fcitx::LotusMode::Off, "disabling RememberModePerWindow must ignore window B's Off memory")) {
        return 1;
    }
    if (!sendKey(engine, entry, *windowB, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window B cycles to Preedit", fcitx::LotusMode::Preedit, "cycling without memory must still update the app rule")) {
        return 1;
    }
    loseFocus(engine, entry, *windowB);

    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A follows the updated app rule", fcitx::LotusMode::Preedit, "without window memory every window must follow the new app rule")) {
        return 1;
    }

    return 0;
}
