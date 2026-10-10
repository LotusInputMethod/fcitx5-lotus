// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app-rule-delay-wire.cpp
 * @brief Headless regression test: a per-app rule's backspace interval and post
 *        delay override the global defaults in the pacer job queued for the
 *        uinput server.
 */

#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <linux/input-event-codes.h>

namespace {

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "Smooth replacement handling accepted or rejected the key unexpectedly");
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
    fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;

    std::vector<StampedKey> stream;
    if (!listener.receiveAll(stream, "the initial Telex replacement stream did not arrive"))
        return 1;
    if (stream.size() != 4) {
        reportFailure("tap stream size", "4 KeyMsgs (2 taps)", std::to_string(stream.size()) + " KeyMsgs", "the Telex replacement must pace 2 backspace taps");
        return 1;
    }
    // N=2 with rule interval=20/post=25: sleeps are [pre=0, max(20,25)=25].
    // Exact 25 (not the 33 default) proves the per-app rule wins.
    {
        std::vector<int64_t> sleeps;
        for (const auto& item : fcitx::KeyPacer::instance().lastPushedJobForTest()) {
            if (const auto* sleep = std::get_if<fcitx::SleepFor>(&item))
                sleeps.push_back(sleep->delay.count());
        }
        if (sleeps.size() != 2 || sleeps[0] != 0 || sleeps[1] != 25) {
            reportFailure("per-app delays on the job", "sleeps=[0,25]",
                          "sleeps=" + std::to_string(sleeps.size()) + (sleeps.empty() ? "" : " last=" + std::to_string(sleeps.back())),
                          "the per-app rule must win over DefaultBackspaceInterval/DefaultPostDelay");
            return 1;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == 2))
            return 1;
    }
    return 0;
}
