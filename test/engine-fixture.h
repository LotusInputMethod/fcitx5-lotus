// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
/**
 * @file engine-fixture.h
 * @brief Key event and focus helpers for the headless tests that drive fcitx::LotusEngine
 *        through TestInputContext.
 */
#pragma once

#include "lotus-engine.h"
#include "test-input-context.h"
#include "test-util.h"

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/event.h>
#include <fcitx/inputmethodentry.h>

#include <string>
#include <string_view>

/**
 * @brief Sends one key event and reports an unexpected acceptance outcome.
 * @param engine Engine under test.
 * @param entry Activated input method entry.
 * @param context Focused test context.
 * @param symbol Keysym to send.
 * @param requireAccepted Whether the engine must accept the key.
 * @param meaning Why the acceptance matters.
 * @return True when event.accepted() matches requireAccepted.
 */
inline bool sendKey(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted,
                    std::string_view meaning = "unexpected key outcome") {
    fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
    engine.keyEvent(entry, event);
    if (event.accepted() != requireAccepted) {
        reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                      "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()), meaning);
        return false;
    }
    return true;
}

/** @brief sendKey overload for callers that build the Key themselves (modifier state, ...). */
inline bool sendKey(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, const fcitx::Key& key, bool requireAccepted,
                    std::string_view meaning = "unexpected key outcome") {
    fcitx::KeyEvent event(&context, key, false);
    engine.keyEvent(entry, event);
    if (event.accepted() != requireAccepted) {
        reportFailure("process key " + std::to_string(key.sym()), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                      "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()), meaning);
        return false;
    }
    return true;
}

/** @brief Reports a focus loss the way a frontend does: deactivate(), then reset(FocusOut). */
inline void loseFocus(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context) {
    fcitx::InputContextEvent out(&context, fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, out);
    engine.reset(entry, out);
    context.focusOut();
}

/** @brief Reports a focus gain the way a frontend does: focusIn(), then activate(). */
inline void gainFocus(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context) {
    context.focusIn();
    fcitx::InputContextEvent in(&context, fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, in);
}

/** @brief Renders the recorded commits as "commits=['a']['b']" for failure reports. */
inline std::string commitList(const TestInputContext& context) {
    std::string actual = "commits=";
    for (const auto& commit : context.commits()) {
        actual += "['" + commit + "']";
    }
    return actual;
}
