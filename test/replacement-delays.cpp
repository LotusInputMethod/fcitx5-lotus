// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file replacement-delays.cpp
 * @brief Headless regression test: the global default delays reach the pacer
 *        job (interval = DefaultBackspaceInterval, post = DefaultPostDelay)
 *        and DefaultCommitInterval delays a replacement after a recent commit.
 *
 * Runs KeyPacer in immediate mode (no EventLoop needed): the whole tap stream
 * lands in the socket buffer synchronously, while SleepFor values stay
 * inspectable via lastPushedJobForTest (no wall-clock involved).
 */

#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <cstddef>
#include <cstdint>
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

    bool isTap(const StampedKey& item, uint16_t code, uint8_t pressed) {
        return item.msg.code == code && item.msg.pressed == pressed;
    }

    // Telex a -> á replacement is 2 backspace taps = 4 KeyMsgs press/rel.
    bool expectTwoTaps(const std::vector<StampedKey>& stream, const char* meaning) {
        if (stream.size() != 4) {
            reportFailure("tap stream size", "4 KeyMsgs (2 taps)", std::to_string(stream.size()) + " KeyMsgs", meaning);
            return false;
        }
        for (size_t i = 0; i < 4; i += 2) {
            if (!isTap(stream[i], KEY_BACKSPACE, 1) || !isTap(stream[i + 1], KEY_BACKSPACE, 0)) {
                reportFailure("tap stream order", "press,release x2", "code=" + std::to_string(stream[i].msg.code), meaning);
                return false;
            }
        }
        return true;
    }

    std::vector<int64_t> sleepMillis(const fcitx::KeyJob& job) {
        std::vector<int64_t> out;
        for (const auto& item : job) {
            if (const auto* sleep = std::get_if<fcitx::SleepFor>(&item))
                out.push_back(sleep->delay.count());
        }
        return out;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-replacement-delays");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DefaultBackspaceInterval", "17");
    config.setValueByPath("DefaultPostDelay", "33");
    config.setValueByPath("DefaultCommitInterval", "1000");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || *engine.config().defaultBackspaceInterval != 17 || *engine.config().defaultPostDelay != 33 ||
        *engine.config().defaultCommitInterval != 1000) {
        reportFailure("configure Smooth/Telex and default delays", "mode=Smooth, intervals 17/33/1000", "configured values differ",
                      "the delay wiring cannot be exercised with the wrong configuration");
        return 1;
    }
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

    // Telex a, s changes the real Bamboo preedit a -> á; Smooth mode replaces
    // the old character through the kb_socket transport.
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;

    std::vector<StampedKey> first;
    if (!listener.receiveAll(first, "the initial Telex replacement stream did not arrive"))
        return 1;
    if (!expectTwoTaps(first, "the initial Telex replacement did not pace 2 backspace taps"))
        return 1;
    // N=2: sleeps are [pre=0 (no prior commit), max(17,33)=33 before last tap].
    {
        auto sleeps = sleepMillis(fcitx::KeyPacer::instance().lastPushedJobForTest());
        if (sleeps.size() != 2 || sleeps[0] != 0 || sleeps[1] != 33) {
            reportFailure("default delays on the first job", "sleeps=[0,33]",
                          "sleeps=" + std::to_string(sleeps.size()) + (sleeps.empty() ? "" : " first=" + std::to_string(sleeps[0])),
                          "DefaultBackspaceInterval/DefaultPostDelay must be forwarded and no commit happened before");
            return 1;
        }
    }

    if (!send(engine, entry, *context, FcitxKey_x, true) || !context->commits().empty()) {
        reportFailure("buffer key x before deletion completes", "no immediate commits", "commits=" + std::to_string(context->commits().size()),
                      "buffered key x was emitted before the first replacement completed");
        return 1;
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == 2))
            return 1;
    }

    std::vector<StampedKey> second;
    if (!listener.receiveAll(second, "buffered key was not replayed after deletion"))
        return 1;
    if (!expectTwoTaps(second, "buffered x replay did not pace 2 backspace taps"))
        return 1;
    // Replay happens right after the previous commit: pre ~= 1000ms cooldown.
    {
        auto sleeps = sleepMillis(fcitx::KeyPacer::instance().lastPushedJobForTest());
        if (sleeps.empty() || sleeps[0] < 700 || sleeps[0] > 1000) {
            reportFailure("commit cooldown on the repeated job", "pre in [700,1000]",
                          sleeps.empty() ? "no sleeps" : "pre=" + std::to_string(sleeps[0]),
                          "DefaultCommitInterval=1000 must delay a replacement that follows the previous commit");
            return 1;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == 2))
            return 1;
    }
    return 0;
}
