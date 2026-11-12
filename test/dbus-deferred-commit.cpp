// SPDX-License-Identifier: GPL-3.0-or-later
//
// The dbus frontend of fcitx5-gtk delivers a commit that is produced while the uinput replacement
// consumes its anchor backspace together with the reply of that key event, and GTK4 apps on Wayland
// then attach the text to the already consumed backspace and drop it (#441, #286). The addon must
// therefore hand the commit to the event loop instead of committing inside the key event.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <fcitx-utils/event.h>
#include <memory>
#include <string>
#include <vector>

namespace {

    bool receiveBackspaceRequest(KbSocketListener& listener, size_t& count, const char* meaning, const char* timeoutExpected = "request within 5000 ms") {
        KbMsg msg{};
        if (!listener.receive(msg, meaning, timeoutExpected))
            return false;
        if (msg.op != KbOp::Backspace || msg.count <= 0) {
            reportFailure("receive replacement request", "op=backspace, count > 0", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                          meaning);
            return false;
        }
        count = msg.count;
        return true;
    }

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "the replacement handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

    // Runs the fcitx event loop long enough for the 0 ms deferred-commit timer to fire.
    void pumpEventLoop(TestInstance& testInstance) {
        auto&      loop      = testInstance.instance.eventLoop();
        const auto exitTimer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 5000, 0, [&loop](fcitx::EventSourceTime*, uint64_t) {
            loop.exit();
            return false;
        });
        loop.exec();
    }

    // Drives one "a" then "s" Telex replacement through the kb_socket transport and replays the
    // echoed backspaces, so the last echo runs finishReplacement().
    std::unique_ptr<TestInputContext> runReplacement(TestInstance& testInstance, fcitx::LotusEngine& engine, KbSocketListener& listener, const char* frontendName) {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        context->setFrontendName(frontendName);
        context->focusIn();
        fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);
        context->resetPreeditUpdateCount();

        if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
            return nullptr;
        size_t backspaces = 0;
        if (!receiveBackspaceRequest(listener, backspaces, "the Telex replacement request did not arrive"))
            return nullptr;
        for (size_t i = 0; i < backspaces; ++i) {
            if (!send(engine, entry, *context, FcitxKey_BackSpace, false))
                return nullptr;
        }
        if (!send(engine, entry, *context, FcitxKey_Pause, true))
            return nullptr;
        return context;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-dbus-deferred-commit");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs", "the test cannot exercise Smooth Telex behavior");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    // Phase 0: a character typed while the previous commit is still deferred must wait for it instead of being
    // forwarded to the application, which used to reorder the word ("tooi" became "tiô").
    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    pendingContext = runReplacement(testInstance, engine, listener, "dbus");
    if (pendingContext == nullptr)
        return 1;
    if (!send(engine, entry, *pendingContext, FcitxKey_i, true))
        return 1;
    if (!pendingContext->commits().empty() || !pendingContext->forwarded().empty()) {
        reportFailure("wait for the deferred commit", "no commit and no forwarded key while the commit is queued",
                      "commits=" + std::to_string(pendingContext->commits().size()) + ", forwarded=" + std::to_string(pendingContext->forwarded().size()),
                      "a character typed before the deferred commit must be buffered, not delivered first");
        return 1;
    }

    // Phase A: a dbus frontend must not receive the commit inside the consumed anchor-backspace event.
    auto dbusContext = runReplacement(testInstance, engine, listener, "dbus");
    if (dbusContext == nullptr)
        return 1;
    if (!dbusContext->commits().empty()) {
        reportFailure("commits deferred out of the key event", "no commit while the anchor backspace is consumed", "commits=" + std::to_string(dbusContext->commits().size()),
                      "a dbus frontend loses the text attached to the consumed backspace");
        return 1;
    }
    pumpEventLoop(testInstance);
    const std::vector<std::string> expected{"á"};
    if (dbusContext->commits() != expected) {
        reportFailure("deferred commit delivered", "one commit of the replaced word", "commits=" + std::to_string(dbusContext->commits().size()),
                      "the deferred commit must reach the client after the key event");
        return 1;
    }
    // The buffered character of Phase 0 is replayed right after its commit, so the client sees "ái" as one commit.
    const std::vector<std::string> pendingExpected{"ái"};
    if (pendingContext->commits() != pendingExpected) {
        reportFailure("buffered key replayed after the commit", "one commit \"ái\"", "commits=" + std::to_string(pendingContext->commits().size()),
                      "the character typed during the deferred commit was not merged after it");
        return 1;
    }

    // Phase B: every other frontend keeps the synchronous commit.
    auto syncContext = runReplacement(testInstance, engine, listener, "test");
    if (syncContext == nullptr)
        return 1;
    if (syncContext->commits() != expected) {
        reportFailure("synchronous commit kept", "one commit right after the key event", "commits=" + std::to_string(syncContext->commits().size()),
                      "frontends other than dbus must keep the immediate commit");
        return 1;
    }

    std::printf("lotus dbus deferred commit: all checks passed\n");
    return 0;
}
