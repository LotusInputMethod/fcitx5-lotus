// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file dict-reload.cpp
 * @brief Headless integration test: applying a configuration must reload the
 *        custom dictionary file.
 *
 * Editing the custom dictionary in the settings GUI rewrites
 * `$XDG_DATA_HOME/fcitx5/lotus/vietnamese.cm.dict` and then re-applies the
 * configuration through D-Bus. The engine must pick the new words up on every
 * apply; loading only at startup forced users to restart fcitx5.
 *
 * The test drives Telex keys through a mock input context: "ddcu" produces
 * "đcu", a word that is not Vietnamese, so it is restored to the raw keys
 * unless the custom dictionary contains it.
 */

#include "lotus-engine.h"
#include "test-input-context.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

    // Helper to log explicit assertion failures with step, expected, actual, and business meaning.
    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

    // Writes the word list to both places the engine may resolve
    // "lotus/vietnamese.cm.dict" from, so the test does not depend on the
    // lookup order between the user data dir and the system data dirs.
    void writeDictionary(const std::filesystem::path& root, const std::string& words) {
        for (const auto& path : {root / "data/fcitx5/lotus/vietnamese.cm.dict", root / "pkgdata/fcitx5/lotus/vietnamese.cm.dict"}) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path);
            file << words;
        }
    }

    // Types the given ASCII keys in a fresh input context and returns the client preedit.
    std::string typePreedit(fcitx::LotusEngine& engine, fcitx::Instance* instance, const std::string& keys) {
        auto context = std::make_unique<TestInputContext>(instance);
        context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
        context->focusIn();
        fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
        fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, in);

        for (const auto key : keys) {
            fcitx::KeyEvent event(context.get(), fcitx::Key(static_cast<fcitx::KeySym>(static_cast<unsigned char>(key))), false);
            engine.keyEvent(entry, event);
        }
        return context->inputPanel().clientPreedit().toString();
    }

    bool expectPreedit(fcitx::LotusEngine& engine, fcitx::Instance* instance, const std::string& keys, const std::string& expected, const std::string& step,
                       const std::string& meaning) {
        const auto actual = typePreedit(engine, instance, keys);
        if (actual != expected) {
            reportFailure(step, expected, actual, meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    const char* testName = "fcitx5-lotus-dict-reload";
    configureTestPaths(testName);

    const auto root = std::filesystem::temp_directory_path() / testName;
    // Keep the lookup away from any dictionary installed on the host.
    setenv("XDG_DATA_DIRS", (root / "pkgdata").c_str(), 1);
    setenv("FCITX_DATA_DIRS", (root / "pkgdata").c_str(), 1);
    writeDictionary(root, "chào\n");

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("EnableDictionary", "True");
    config.setValueByPath("SpellCheck", "True");
    config.setValueByPath("AutoNonVnRestore", "True");
    engine.setConfig(config);

    bool ok = true;

    // Step 1: "đcu" is not in the dictionary yet, so the word falls back to the raw keys.
    ok &= expectPreedit(engine, &testInstance.instance, "ddcu", "ddcu", "type ddcu with dictionary without the word",
                        "a non-Vietnamese word outside the dictionary must be restored to the raw keystrokes");

    // Step 2: the settings GUI writes the new word and re-applies the configuration.
    writeDictionary(root, "đcu\n");
    engine.setConfig(config);

    // Step 3: the word added on disk must be honoured without restarting fcitx5.
    ok &= expectPreedit(engine, &testInstance.instance, "ddcu", "đcu", "type ddcu after adding the word and re-applying the configuration",
                        "applying a configuration must reload the custom dictionary");

    // Step 4: removing the word again must be honoured as well.
    writeDictionary(root, "chào\n");
    engine.setConfig(config);
    ok &= expectPreedit(engine, &testInstance.instance, "ddcu", "ddcu", "type ddcu after removing the word and re-applying the configuration",
                        "reloading the dictionary must replace its content, not merge");

    return ok ? 0 : 1;
}
