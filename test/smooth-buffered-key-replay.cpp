// SPDX-License-Identifier: GPL-3.0-or-later
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
                          "Smooth buffered-key handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

    // Counts backspace taps (press events) in a drained stream.
    bool countTaps(const std::vector<StampedKey>& stream, size_t& taps, const char* meaning) {
        taps = 0;
        for (size_t i = 0; i < stream.size(); ++i) {
            const bool press = (i % 2 == 0);
            if (stream[i].msg.code != KEY_BACKSPACE || stream[i].msg.pressed != (press ? 1 : 0)) {
                reportFailure("tap stream order", "press,release pairs of BackSpace", "index=" + std::to_string(i) + " code=" + std::to_string(stream[i].msg.code),
                              meaning);
                return false;
            }
            if (press)
                ++taps;
        }
        if (taps == 0 || (stream.size() % 2) != 0) {
            reportFailure("tap stream size", "non-empty press/release pairs", std::to_string(stream.size()) + " KeyMsgs", meaning);
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-smooth-buffered-key-replay");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs",
                      "the replay test cannot exercise Smooth Telex behavior");
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

    // Telex a, s changes the real Bamboo preedit a -> á. Smooth mode replaces
    // the old character through the kb_socket transport.
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    std::vector<StampedKey> first;
    if (!listener.receiveAll(first, "the initial Telex replacement stream did not arrive"))
        return 1;
    size_t backspaces = 0;
    if (!countTaps(first, backspaces, "the initial Telex replacement did not pace backspace taps"))
        return 1;

    if (!send(engine, entry, *context, FcitxKey_x, true) || !context->commits().empty()) {
        reportFailure("buffer key x before deletion completes", "no immediate commits", "commits=" + std::to_string(context->commits().size()),
                      "buffered key x was emitted before the first replacement completed");
        return 1;
    }
    for (size_t i = 0; i < backspaces; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == backspaces))
            return 1;
    }

    std::vector<StampedKey> second;
    if (!listener.receiveAll(second, "buffered key was not replayed after deletion"))
        return 1;
    size_t replayBackspaces = 0;
    if (!countTaps(second, replayBackspaces, "buffered x replay did not pace backspace taps"))
        return 1;
    for (size_t i = 0; i < replayBackspaces; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == replayBackspaces))
            return 1;
    }

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        std::string actual = "commits=";
        for (const auto& commit : context->commits())
            actual += "['" + commit + "']";
        const auto meaning = "the buffered key replay did not produce the expected final commits; "
                             "first-taps=" +
            std::to_string(backspaces) + ", replay-taps=" + std::to_string(replayBackspaces);
        reportFailure("verify final replay commits", "commits=['á']['ã']", actual, meaning);
        return 1;
    }
    return 0;
}
