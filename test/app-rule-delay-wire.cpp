// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app-rule-delay-wire.cpp
 * @brief Headless regression test: a per-app rule's backspace interval and post
 *        delay override the global defaults on the KbMsg sent to the uinput
 *        server.
 */

#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

    bool receiveBackspaceRequest(KbSocketListener& listener, KbMsg& msg, const char* meaning, const char* timeoutExpected = "request within 5000 ms") {
        if (!listener.receive(msg, meaning, timeoutExpected))
            return false;
        if (msg.op != KbOp::Backspace || msg.count <= 0) {
            reportFailure("receive replacement request", "op=backspace, count > 0", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                          meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    const char* testName = "fcitx5-lotus-app-rule-delay-wire";
    configureTestPaths(testName);

    // The mock input context reports the program name "test"; its rule sets the
    // backspace interval to 20 ms and the post delay to 25 ms.
    const auto rulesFile = std::filesystem::temp_directory_path() / testName / "config/fcitx5/conf/lotus-app-rules.conf";
    {
        std::filesystem::create_directories(rulesFile.parent_path());
        std::ofstream file(rulesFile);
        file << "test=1,0,20,25\n";
    }

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DefaultBackspaceInterval", "17");
    config.setValueByPath("DefaultPostDelay", "33");
    engine.setConfig(config);

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    if (!sendKey(engine, entry, *context, FcitxKey_a, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_s, true, "the trigger key must be swallowed to start the replacement"))
        return 1;

    KbMsg msg{};
    if (!receiveBackspaceRequest(listener, msg, "the initial Telex replacement request did not arrive"))
        return 1;
    if (msg.interval != 20 || msg.postDelay != 25) {
        reportFailure("per-app delays on the request", "interval=20, postDelay=25", "interval=" + std::to_string(msg.interval) + ", postDelay=" + std::to_string(msg.postDelay),
                      "the per-app rule must win over DefaultBackspaceInterval/DefaultPostDelay");
        return 1;
    }
    for (size_t i = 0; i < msg.count; ++i) {
        if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the burst backspace must echo to the app so it deletes the old word"))
            return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement"))
        return 1;
    return 0;
}
