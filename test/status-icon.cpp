// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file status-icon.cpp
 * @brief Verify tray icon selection independently of application colour preferences.
 */

#include "lotus-engine.h"
#include "test-input-context.h"
#include "test-utils.h"

#include <array>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include <fcitx/event.h>
#include <fcitx/inputmethodentry.h>

int main(int argc, char** argv) {
    const auto args  = std::span(argv, static_cast<std::size_t>(argc));
    const bool gnome = args.size() > 1 && std::string_view(args[1]) == "gnome";
    configureTestPaths(gnome ? "fcitx5-lotus-status-icon-gnome" : "fcitx5-lotus-status-icon-other");
    setenv("XDG_CURRENT_DESKTOP", gnome ? "ubuntu:GNOME" : "XFCE", 1);
    // A deterministic dark application preference must not decide the colour
    // of GNOME's independently themed panel. Other desktops keep this probe.
    setenv("GTK_THEME", "Adwaita:dark", 1);

    TestInstance            testInstance;
    fcitx::LotusEngine      engine(&testInstance.instance);
    TestInputContext        context(&testInstance.instance);
    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    context.focusIn();

    const std::array<std::array<const char*, 2>, 3> modes{
        {
            {"Preedit", "fcitx-lotus"},
            {"OFF", "fcitx-lotus-off"},
            {"Emoji Picker", "fcitx-lotus-emoji"},
        },
    };
    for (const auto& [mode, base] : modes) {
        for (const auto* theme : {"Auto", "Light", "Dark"}) {
            for (const bool lotusIcons : {false, true}) {
                fcitx::RawConfig config;
                config.setValueByPath("Mode", mode);
                config.setValueByPath("IconTheme", theme);
                config.setValueByPath("UseLotusIcons", lotusIcons ? "True" : "False");
                engine.setConfig(config);
                fcitx::InputContextEvent focus(&context, fcitx::EventType::InputContextFocusIn);
                engine.activate(entry, focus);

                std::string expected = base;
                if (!lotusIcons) {
                    if (std::string_view(theme) == "Light") {
                        expected += "-default-black";
                    } else if (gnome && std::string_view(theme) == "Auto") {
                        expected += "-default-symbolic";
                    } else {
                        expected += "-default";
                    }
                }
                const auto actual = engine.subModeIcon(entry, context);
                if (actual != expected) {
                    reportFailure(std::string(mode) + "/" + theme + "/" + (lotusIcons ? "Lotus" : "text") + " tray icon", expected, actual,
                                  "tray icon selection must follow the mode, theme and icon preference rules");
                }
            }
        }
    }
    return checkExitCode("lotus status icon: all checks passed");
}
