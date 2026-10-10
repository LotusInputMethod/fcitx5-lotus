// SPDX-License-Identifier: GPL-3.0-or-later
//
// Select mode: a replacement is delivered as a Shift+Left selection stream on
// the keyboard socket instead of backspace taps. The engine commits the
// replacement once all echoed left arrows have arrived, the intermediate arrows
// pass through so the app can extend the selection, and no arrow is replayed
// afterwards.
#include "fcitx-utils/keysym.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <linux/input-event-codes.h>

namespace {

    // Parses a selection stream: Shift-down, N x Left taps, Shift-up(10ms),
    // Delete tap, post, final Left tap. Returns N (Left presses before Shift-up).
    bool parseSelectStream(const std::vector<StampedKey>& stream, size_t& selects, const char* meaning) {
        selects     = 0;
        size_t i    = 0;
        auto   fail = [&](const std::string& expected) {
            reportFailure("parse selection stream", expected, "items=" + std::to_string(stream.size()) + " parsed=" + std::to_string(i), meaning);
            return false;
        };
        auto need = [&](uint16_t code, uint8_t pressed) { return i < stream.size() && stream[i].msg.code == code && stream[i].msg.pressed == pressed; };
        if (!need(KEY_LEFTSHIFT, 1))
            return fail("Shift-down first");
        ++i;
        while (i + 1 < stream.size() && stream[i].msg.code == KEY_LEFT && stream[i + 1].msg.code == KEY_LEFT) {
            if (stream[i].msg.pressed != 1 || stream[i + 1].msg.pressed != 0)
                return fail("Left press/release pairs");
            ++selects;
            i += 2;
        }
        if (selects == 0)
            return fail("at least 1 Left tap");
        if (!need(KEY_LEFTSHIFT, 0))
            return fail("Shift-up after Left taps");
        ++i;
        if (!need(KEY_DELETE, 1))
            return fail("Delete press after Shift-up");
        ++i;
        if (!need(KEY_DELETE, 0))
            return fail("Delete release after Shift-up");
        ++i;
        if (!need(KEY_LEFT, 1))
            return fail("final Left press");
        ++i;
        if (!need(KEY_LEFT, 0))
            return fail("final Left release");
        ++i;
        if (i != stream.size())
            return fail("no trailing items");
        return true;
    }

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "Select echo handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

    std::string commitList(const TestInputContext& context) {
        std::string actual = "commits=";
        for (const auto& commit : context.commits())
            actual += "['" + commit + "']";
        return actual;
    }

    bool drainSelect(KbSocketListener& listener, size_t& selects, const char* meaning) {
        std::vector<StampedKey> stream;
        if (!listener.receiveAll(stream, meaning))
            return false;
        return parseSelectStream(stream, selects, meaning);
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-mode-replacement");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Select || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Select/Telex", "mode=Select, input method=Telex", "configured mode or input method differs",
                      "the select test cannot exercise Select mode Telex behavior");
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

    // Telex a, s changes the Bamboo preedit a -> á. Select mode must ask the
    // uinput server to select the old character(s) instead of sending backspaces.
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    size_t selects = 0;
    if (!drainSelect(listener, selects, "the initial Telex replacement did not stream a selection"))
        return 1;

    // A key typed while the selection is in flight must not commit immediately.
    if (!send(engine, entry, *context, FcitxKey_x, true) || !context->commits().empty()) {
        reportFailure("buffer key x before the selection completes", "no immediate commits", commitList(*context), "buffered key x was emitted before the selection completed");
        return 1;
    }

    // Echoed left arrows: every press but the last passes through so the app can
    // extend the selection; the last one is swallowed and commits the replacement.
    for (size_t i = 0; i < selects - 1; ++i) {
        if (!send(engine, entry, *context, FcitxKey_Left, false))
            return 1;
    }
    if (!send(engine, entry, *context, FcitxKey_Delete, false))
        return 1;
    if (!send(engine, entry, *context, FcitxKey_Left, true))
        return 1;
    const std::vector<std::string> afterFirst{"á"};
    if (context->commits() != afterFirst) {
        reportFailure("verify commit after the last left arrow", "commits=['á']", commitList(*context) + ", selects=" + std::to_string(selects),
                      "the replacement was not committed exactly once when the selection completed");
        return 1;
    }

    // The buffered x is replayed after the commit and starts a second selection.
    // No left arrow is replayed: only one new stream arrives and it matches the
    // count the replacement needs.
    size_t replaySelects = 0;
    if (!drainSelect(listener, replaySelects, "buffered key was not replayed after the selection completed"))
        return 1;
    for (size_t i = 0; i < replaySelects - 1; ++i) {
        if (!send(engine, entry, *context, FcitxKey_Left, false))
            return 1;
    }
    if (!send(engine, entry, *context, FcitxKey_Delete, false))
        return 1;
    if (!send(engine, entry, *context, FcitxKey_Left, true))
        return 1;

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        reportFailure("verify final replay commits", "commits=['á']['ã']",
                      commitList(*context) + ", first-selects=" + std::to_string(selects) + ", replay-selects=" + std::to_string(replaySelects),
                      "the buffered key replay did not produce the expected final commits, or a left arrow was replayed");
        return 1;
    }
    return 0;
}
