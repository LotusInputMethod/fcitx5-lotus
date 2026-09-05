// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file preedit-lifecycle.cpp
 * @brief Headless integration test for client-side preedit lifecycle & focus transitions.
 *
 * Architectural Contract:
 * In Fcitx5, client applications supporting inline preedit (`CapabilityFlag::Preedit`)
 * render uncommitted composition directly inside their text widgets (e.g. Qt, GTK, Electron).
 *
 * When an input context undergoes focus or lifecycle transitions:
 * 1. Active Composition: Typing keys (e.g. 'a', 's' in Telex) must update the client
 *    preedit text to "á" without prematurely committing text to the document.
 * 2. Focus Loss (`FocusOut` / Deactivate): When the user switches windows or clicks outside,
 *    the active preedit composition MUST be committed to the document exactly once,
 *    and the client preedit buffer must be cleared.
 * 3. Focus Regain (`FocusIn` / Activate): When returning focus to the application, the
 *    preedit buffer must remain clean. No stale ghost text or duplicate commits should occur.
 *
 * This test uses `TestInputContext` and `TestInstance` to deterministically verify these
 * state transitions without a running X11/Wayland desktop session.
 */

#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/utf8.h>
#include <iostream>
#include <string>
#include <vector>

namespace {

    // Helper to log explicit assertion failures with step, expected, actual, and business meaning.
    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

} // namespace

int main() {
    // Step 0: Sandbox filesystem paths to /tmp/fcitx5-lotus-preedit-lifecycle
    configureTestPaths("fcitx5-lotus-preedit-lifecycle");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    // Step 1: Configure Lotus in Preedit mode with Telex input method
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Preedit || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Preedit/Telex", "mode=Preedit, input method=Telex", "configured mode or input method differs",
                      "the lifecycle test cannot exercise client preedit behavior");
        return 1;
    }

    // Step 2: Initialize mock InputContext with Preedit capability (emulating inline client app like GTK/Qt)
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");

    fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, in);
    context->resetPreeditUpdateCount();

    // Step 3: Type Telex keys 'a' followed by 's' -> should compose Vietnamese character "á"
    for (auto sym : {FcitxKey_a, FcitxKey_s}) {
        fcitx::KeyEvent event(context.get(), fcitx::Key(sym), false);
        engine.keyEvent(entry, event);
        if (!event.accepted()) {
            reportFailure("type Telex key", "key event accepted", "key " + std::to_string(sym) + " was not accepted", "Lotus did not process the Telex input");
            return 1;
        }
    }

    // Step 4: Verify client preedit state during active composition
    // - Client preedit must hold "á" (valid UTF-8, 2 bytes, cursor at end).
    // - Server preedit (candidate window) must be empty.
    // - Commits must be 0 (composition is still active, not yet committed).
    // - updatePreedit() must have been called twice (once for 'a', once for 's').
    const auto& clientPreedit = context->inputPanel().clientPreedit();
    const auto& serverPreedit = context->inputPanel().preedit();
    if (clientPreedit.toString() != "á" || !fcitx::utf8::validate(clientPreedit.toString()) || clientPreedit.cursor() != 2 || !context->commits().empty() ||
        !serverPreedit.toString().empty() || context->preeditUpdates() != 2) {
        reportFailure("type a then s in Preedit/Telex", "client preedit=á (valid UTF-8, cursor=2), server preedit empty, commits=0, updatePreedit count=2",
                      "client preedit=" + clientPreedit.toString() + ", cursor=" + std::to_string(clientPreedit.cursor()) + ", server preedit=" + serverPreedit.toString() +
                          ", commits=" + std::to_string(context->commits().size()) + ", updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "Preedit mode must expose the composed Telex result through client preedit without committing it");
        return 1;
    }

    // Step 5: Emulate FocusOut (user switches windows or clicks outside)
    // - Active preedit "á" must be committed to the client application exactly once.
    const std::vector<std::string> expectedCommits{"á"};
    context->focusOut();
    if (context->commits() != expectedCommits) {
        reportFailure("focus out with active client preedit", "commits=[á]",
                      "commit count=" + std::to_string(context->commits().size()) + (context->commits().empty() ? "" : ", first commit=" + context->commits().front()),
                      "Fcitx must commit the active client preedit exactly once when the client does not handle unfocus commits");
        return 1;
    }

    // Step 6: Emulate deactivation on FocusOut
    // - Engine must clear client preedit buffer with one additional update call (total updates = 3).
    fcitx::InputContextEvent out(context.get(), fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, out);
    if (!context->inputPanel().clientPreedit().toString().empty() || context->preeditUpdates() != 3) {
        reportFailure("deactivate on focus out", "client preedit empty, updatePreedit count=3",
                      "client preedit=" + context->inputPanel().clientPreedit().toString() + ", updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "focus-out deactivation must clear the client preedit with one additional update");
        return 1;
    }

    // Step 7: Emulate FocusIn & reactivation (user refocuses the application)
    // - Engine reactivation must keep the preedit buffer empty.
    // - No duplicate commits or lingering ghost characters should be emitted.
    context->focusIn();
    fcitx::InputContextEvent reactivate(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, reactivate);
    if (!context->inputPanel().clientPreedit().toString().empty() || context->commits() != expectedCommits) {
        reportFailure("reactivate after focus out", "client preedit empty, commits=[á]",
                      "client preedit=" + context->inputPanel().clientPreedit().toString() + ", commit count=" + std::to_string(context->commits().size()),
                      "reactivation must preserve the cleared preedit without committing stale text");
        return 1;
    }

    return 0; // All lifecycle invariants verified successfully
}
