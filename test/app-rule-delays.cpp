// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app-rule-delays.cpp
 * @brief Headless test: per-app rules parse the mode plus the three delay
 *        overrides (commit/backspace/post) from lotus-app-rules.conf, treat
 *        omitted fields as "use defaults", keep the legacy mode id 2 working,
 *        and skip malformed lines without breaking the load.
 */

#include "lotus-engine.h"
#include "test-input-context.h"
#include "test-util.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

    std::string settingText(const fcitx::LotusAppRuleSetting& setting) {
        return "mode=" + std::to_string(static_cast<int>(setting.mode)) + " commit=" + std::to_string(setting.commitInterval) +
            " backspace=" + std::to_string(setting.backspaceInterval) + " post=" + std::to_string(setting.postDelay);
    }

    bool expectSetting(const fcitx::LotusEngine& engine, const std::string& app, const fcitx::LotusAppRuleSetting& expected, const std::string& meaning) {
        const auto actual = engine.getAppRuleSetting(app);
        if (actual.mode != expected.mode || actual.commitInterval != expected.commitInterval || actual.backspaceInterval != expected.backspaceInterval ||
            actual.postDelay != expected.postDelay) {
            reportFailure("app rule for " + app, settingText(expected), settingText(actual), meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    const char* testName = "fcitx5-lotus-app-rule-delays";
    configureTestPaths(testName);

    const auto rulesFile = std::filesystem::temp_directory_path() / testName / "config/fcitx5/conf/lotus-app-rules.conf";
    {
        std::filesystem::create_directories(rulesFile.parent_path());
        std::ofstream file(rulesFile);
        file << "# comment\n";
        file << "test=1,40,20,25\n";
        file << "legacy=2\n";
        file << "partial=3,15\n";
        file << "bare=5\n";
        file << "malformed=abc\n";
    }

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);

    bool ok = true;
    ok &= expectSetting(engine, "test", {.mode = fcitx::LotusMode::Smooth, .commitInterval = 40, .backspaceInterval = 20, .postDelay = 25},
                        "app=mode,commit,backspace,post must be parsed field by field");
    ok &= expectSetting(engine, "legacy", {.mode = fcitx::LotusMode::Smooth}, "legacy mode id 2 must resolve to Smooth");
    ok &= expectSetting(engine, "partial", {.mode = fcitx::LotusMode::SuperSmooth, .commitInterval = 15}, "omitted trailing delay fields must stay zero");
    ok &= expectSetting(engine, "bare", {.mode = fcitx::LotusMode::Preedit}, "a mode-only rule must keep the parsed mode");
    ok &= expectSetting(engine, "malformed", {.mode = fcitx::LotusMode::Preedit}, "a malformed line must be skipped");
    ok &= expectSetting(engine, "khong-co-rule", {.mode = fcitx::LotusMode::Preedit}, "an app without a rule must inherit the global mode");
    return ok ? 0 : 1;
}
