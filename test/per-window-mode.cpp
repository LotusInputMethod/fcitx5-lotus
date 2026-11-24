// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file per-window-mode.cpp
 * @brief Headless regression test: each window remembers the input mode last
 *        chosen in it, without leaking that choice to other windows.
 *
 * Two input contexts share the program name "test".  With per-window memory
 * enabled, cycling in window A must not write the per-app rule, so window B
 * keeps the global mode until it makes its own choice; each window then
 * restores its own mode on focus.  The Default menu entry forgets only the
 * choosing window's memory.  Disabling RememberModePerWindow restores the
 * legacy behavior where cycling writes the per-app rule for every window.
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
            case fcitx::LotusMode::SuperSmooth: return "Smooth (Super)";
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
    config.setValueByPath("ModeMenuKey/0", "grave");
    config.setValueByPath("ShortcutDefault", "r");
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
    if (!expectMode("window B keeps the global mode", fcitx::LotusMode::Preedit, "window A's choice must not leak to window B through the per-app rule")) {
        return 1;
    }

    if (!sendKey(engine, entry, *windowB, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window B cycles to Emoji", fcitx::LotusMode::Emoji, "cycling must switch the focused window to the next mode")) {
        return 1;
    }
    if (!sendKey(engine, entry, *windowB, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window B cycles to Off", fcitx::LotusMode::Off, "cycling must switch the focused window to the next mode")) {
        return 1;
    }

    loseFocus(engine, entry, *windowB);
    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A restores its own Emoji mode", fcitx::LotusMode::Emoji, "window A must remember Emoji even though the global mode is Off")) {
        return 1;
    }

    loseFocus(engine, entry, *windowA);
    gainFocus(engine, entry, *windowB);
    if (!expectMode("window B restores its own Off mode", fcitx::LotusMode::Off, "each window must keep its own remembered mode")) {
        return 1;
    }
    loseFocus(engine, entry, *windowB);

    gainFocus(engine, entry, *windowA);
    if (!sendKey(engine, entry, *windowA, FcitxKey_grave, true, "the mode menu hotkey must be consumed")) {
        return 1;
    }
    if (!sendKey(engine, entry, *windowA, FcitxKey_r, true, "the Default entry key must be consumed")) {
        return 1;
    }
    if (!expectMode("the Default entry resets window A", fcitx::LotusMode::Preedit, "choosing Default Typing must drop only window A's memory")) {
        return 1;
    }
    loseFocus(engine, entry, *windowA);

    gainFocus(engine, entry, *windowB);
    if (!expectMode("window B keeps its own Off mode", fcitx::LotusMode::Off, "window A's Default choice must not clear window B's memory")) {
        return 1;
    }
    loseFocus(engine, entry, *windowB);

    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A follows the global mode after Default", fcitx::LotusMode::Preedit, "window A must have no remembered mode left")) {
        return 1;
    }
    loseFocus(engine, entry, *windowA);

    fcitx::RawConfig disabled;
    disabled.setValueByPath("Mode", "Preedit");
    disabled.setValueByPath("InputMethod", "Telex");
    disabled.setValueByPath("CycleModeKey/0", "F9");
    disabled.setValueByPath("ModeMenuKey/0", "grave");
    disabled.setValueByPath("ShortcutDefault", "r");
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
    if (!expectMode("window A ignores its memory when disabled", fcitx::LotusMode::Preedit, "disabling RememberModePerWindow must fall back to the global mode")) {
        return 1;
    }
    if (!sendKey(engine, entry, *windowA, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window A cycles to Emoji", fcitx::LotusMode::Emoji, "cycling without memory must still update the mode")) {
        return 1;
    }
    loseFocus(engine, entry, *windowA);

    gainFocus(engine, entry, *windowB);
    if (!expectMode("window B follows the per-app rule when memory is disabled", fcitx::LotusMode::Emoji, "without window memory every window must follow the cycled app rule")) {
        return 1;
    }
    if (!sendKey(engine, entry, *windowB, FcitxKey_F9, true, "the cycle hotkey must be consumed")) {
        return 1;
    }
    if (!expectMode("window B cycles to Off", fcitx::LotusMode::Off, "cycling must update the per-app rule and the focused window")) {
        return 1;
    }
    loseFocus(engine, entry, *windowB);

    gainFocus(engine, entry, *windowA);
    if (!expectMode("window A follows the updated app rule", fcitx::LotusMode::Off, "without window memory every window must follow the new app rule")) {
        return 1;
    }

    return 0;
}
