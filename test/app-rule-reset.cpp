// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app-rule-reset.cpp
 * @brief Headless regression test: a per-app mode rule must survive a config
 *        reload / new input context for the focused window.
 *
 * Bug: LotusState::setEngine() wrote the global `realMode` from the global Mode
 * option without resolving the per-app rule.  setEngine() runs on every new
 * input context and for every input context on config reload (refreshEngine),
 * so a focused app with its own rule (e.g. Off) silently dropped back to the
 * global mode (e.g. Preedit) until the next focus change.
 *
 * The test focuses a context whose program ("test") has an Off rule while the
 * global mode is Preedit, then reloads the config without changing focus and
 * asserts the resolved mode is still the rule (Off).
 */

#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

    std::string modeName(fcitx::LotusMode mode) {
        switch (mode) {
            case fcitx::LotusMode::Off: return "Off";
            case fcitx::LotusMode::Preedit: return "Preedit";
            case fcitx::LotusMode::Smooth: return "Smooth";
            case fcitx::LotusMode::Uinput: return "Uinput";
            case fcitx::LotusMode::SuperSmooth: return "SuperSmooth";
            case fcitx::LotusMode::SurroundingText: return "SurroundingText";
            case fcitx::LotusMode::Emoji: return "Emoji";
            case fcitx::LotusMode::Minecraft: return "Minecraft";
            default: return "Unknown";
        }
    }

} // namespace

int main() {
    const char* testName = "fcitx5-lotus-app-rule-reset";
    configureTestPaths(testName);

    // Per-app rule for the mock context's program name ("test"): Off.
    // Must be on disk before the engine is constructed (loadAppRules runs in
    // the constructor).
    const auto rulesFile = std::filesystem::temp_directory_path() / testName / "config/fcitx5/conf/lotus-app-rules.conf";
    {
        std::ofstream file(rulesFile, std::ios::trunc);
        if (!file.is_open()) {
            reportFailure("write app rules file", "file open", rulesFile.string(), "the test needs the app rule on disk");
            return 1;
        }
        file << "test=0\n";
    }

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    // Global mode Preedit, differing from the app's Off rule.
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Preedit) {
        reportFailure("configure global Preedit", "mode=Preedit", "global mode differs", "the test needs the global mode to differ from the app rule");
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    if (::realMode.load() != fcitx::LotusMode::Off) {
        reportFailure("activate with rule Off", "realMode=Off", "realMode=" + modeName(::realMode.load()), "activate() must resolve the per-app rule for the focused window");
        return 1;
    }

    // Simulate a config reload / a new input context appearing without any
    // focus change: setEngine() runs for every context but must not clobber the
    // focused window's resolved rule.
    fcitx::RawConfig reloaded;
    reloaded.setValueByPath("Mode", "Preedit");
    reloaded.setValueByPath("InputMethod", "Telex");
    engine.setConfig(reloaded);

    if (::realMode.load() != fcitx::LotusMode::Off) {
        reportFailure("config reload keeps focused app rule", "realMode=Off", "realMode=" + modeName(::realMode.load()),
                      "a config reload must not reset the focused window from its per-app rule back to the global mode");
        return 1;
    }

    return 0;
}
