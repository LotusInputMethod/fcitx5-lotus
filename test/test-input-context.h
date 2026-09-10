// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

/**
 * @brief Headless Mock InputContext for Fcitx5-Lotus Integration Testing.
 *
 * Architecture & Rationale:
 * Fcitx5 input method engines interact with client applications (text editors,
 * browsers, terminals) through an abstract `fcitx::InputContext` interface. In a
 * live desktop environment, concrete implementations (like X11, Wayland, or D-Bus
 * frontends) handle IPC and render UI.
 *
 * In headless integration tests, we need to verify engine behavior (commits,
 * backspaces, forwarded keys, preedit updates) without running a display server
 * (X11/Wayland) or D-Bus daemon. `TestInputContext` subclasses `fcitx::InputContext`
 * directly, intercepting all engine output callbacks and recording them into
 * in-memory vectors for synchronous, deterministic assertions.
 *
 * Example — Writing a new integration test:
 * @code
 * int main() {
 *     // 1. Sandbox filesystem paths to /tmp/fcitx5-lotus-my-test
 *     configureTestPaths("fcitx5-lotus-my-test");
 *
 *     // 2. Initialize headless Fcitx instance and Lotus engine
 *     TestInstance testInstance;
 *     fcitx::LotusEngine engine(&testInstance.instance);
 *
 *     // 3. Set engine mode and input method
 *     fcitx::RawConfig config;
 *     config.setValueByPath("Mode", "Preedit"); // or "Surrounding", "Smooth", "Off"
 *     config.setValueByPath("InputMethod", "Telex");
 *     engine.setConfig(config);
 *
 *     // 4. Create mock context with client capabilities and activate
 *     auto context = std::make_unique<TestInputContext>(&testInstance.instance);
 *     context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
 *     context->focusIn();
 *     fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
 *     fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
 *     engine.activate(entry, in);
 *
 *     // 5. Send keystrokes to the engine
 *     fcitx::KeyEvent event(context.get(), fcitx::Key(FcitxKey_a), false);
 *     engine.keyEvent(entry, event);
 *
 *     // 6. Assert on recorded outputs
 *     // - Commits:        context->commits()
 *     // - Deletions:      context->deletes()
 *     // - Forwarded keys: context->forwarded()
 *     // - Client preedit: context->inputPanel().clientPreedit().toString()
 *     if (!event.accepted() || context->inputPanel().clientPreedit().toString() != "a") {
 *         return 1; // Test failed
 *     }
 *     return 0; // Test passed
 * }
 * @endcode
 *
 * CMake Registration (test/CMakeLists.txt):
 * @code
 * add_lotus_headless_test(my_test my-test.cpp "integration;myfeature")
 * @endcode
 */
class TestInputContext final : public fcitx::InputContext {
  public:
    explicit TestInputContext(fcitx::Instance* instance) : InputContext(instance->inputContextManager(), "test") {
        // Fcitx5 lifecycle contract: notifies the InputContextManager that
        // this context is initialized and ready to receive events.
        created();
    }

    ~TestInputContext() override {
        // Fcitx5 lifecycle contract: detaches context and invalidates event slots.
        destroy();
    }

    const char* frontend() const override {
        return "test";
    }

    // Called by the engine when text is committed to the client application.
    void commitStringImpl(const std::string& text) override {
        commits_.push_back(text);
    }

    // Called when the engine requests surrounding text deletion (e.g. backspacing
    // prior characters during composition or word replacement).
    void deleteSurroundingTextImpl(int offset, unsigned int size) override {
        deletes_.emplace_back(offset, size);
    }

    // Called when a key event is not consumed by the engine and forwarded
    // back to the client application (passthrough mode).
    void forwardKeyImpl(const fcitx::ForwardKeyEvent& event) override {
        forwarded_.push_back(event);
    }

    // Called when inline preedit text or server candidate window changes.
    void updatePreeditImpl() override {
        ++preeditUpdates_;
    }

    // Verification helpers: inspect recorded engine actions
    void resetPreeditUpdateCount() {
        preeditUpdates_ = 0;
    }
    unsigned int preeditUpdates() const {
        return preeditUpdates_;
    }
    const std::vector<std::string>& commits() const {
        return commits_;
    }
    const auto& deletes() const {
        return deletes_;
    }
    const auto& forwarded() const {
        return forwarded_;
    }

  private:
    std::vector<std::string>                  commits_;
    std::vector<std::pair<int, unsigned int>> deletes_;
    std::vector<fcitx::ForwardKeyEvent>       forwarded_;
    unsigned int                              preeditUpdates_ = 0;
};

/**
 * @brief Sandboxes environment paths to isolate tests from host user configuration.
 *
 * Fcitx5 engines and Bamboo read/write user configuration, dictionaries, and cache
 * under $HOME, $XDG_CONFIG_HOME, $XDG_DATA_HOME, and $XDG_CACHE_HOME.
 * Calling `configureTestPaths` redirects all these paths to an ephemeral subdirectory
 * in the system temporary folder (`/tmp/<name>`).
 *
 * Guarantees:
 * 1. Clean slate: Previous runs or local user configs will not contaminate test results.
 * 2. Host safety: Tests will NEVER overwrite developer's personal fcitx5 settings.
 * 3. Parallel-safe: Each test target passes a unique test name for isolated folders.
 */
inline void configureTestPaths(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "config/fcitx5/conf");
    std::filesystem::create_directories(root / "data");
    std::filesystem::create_directories(root / "cache");
    setenv("HOME", root.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1);
}

/**
 * @brief Headless Fcitx5 instance environment isolator.
 *
 * Instantiates a minimal, headless `fcitx::Instance` with:
 * - `--disable=all`: disables loading external modules (UI, D-Bus, Wayland/X11 frontends),
 *   allowing the instance to initialize in pure headless/CI containers without hanging.
 * - `registerDefaultLoader(nullptr)`: prevents dynamic plugin loader from scanning
 *   system directories (`/usr/lib/fcitx5`) for host-installed addons.
 */
struct TestInstance {
    TestInstance() : instance(2, argv) {
        instance.addonManager().registerDefaultLoader(nullptr);
        instance.initialize();
    }
    char            program[64]    = "lotus-headless-test";
    char            disableAll[16] = "--disable=all";
    char*           argv[3]        = {program, disableAll, nullptr};
    fcitx::Instance instance;
};
