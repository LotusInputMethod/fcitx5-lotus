// SPDX-License-Identifier: GPL-3.0-or-later
//
// One replacement must produce exactly one commitString: the replaced word and the text of every key
// that was buffered during the deletion belong in the same commit. The text-input-v3 state behind
// gnome-shell's ibus frontend (Chromium/Brave native Wayland) is double buffered, so a second
// commit_string before the next done event replaces the first one and the replaced word is lost
// (#594). The dbus frontend keeps deferring the batch out of the key event (#441, #286).
#include "engine-fixture.h"
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

    // Runs the fcitx event loop long enough for the 0 ms deferred-commit timer to fire.
    void pumpEventLoop(TestInstance& testInstance) {
        auto&      loop      = testInstance.instance.eventLoop();
        const auto exitTimer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 5000, 0, [&loop](fcitx::EventSourceTime*, uint64_t) {
            loop.exit();
            return false;
        });
        loop.exec();
    }

    // Telex "oo" -> "ô" starts a replacement, "i" is typed while the deletion runs and gets buffered,
    // so finishing the replacement replays it and its text lands in the same batch as the "ô".
    std::unique_ptr<TestInputContext> runReplacementWithBufferedKey(TestInstance& testInstance, fcitx::LotusEngine& engine, KbSocketListener& listener, const char* frontendName) {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        context->setFrontendName(frontendName);
        context->focusIn();
        fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);
        context->resetPreeditUpdateCount();

        if (!sendKey(engine, entry, *context, FcitxKey_o, false, "plain letters before the trigger must echo to the app") ||
            !sendKey(engine, entry, *context, FcitxKey_o, true, "the trigger key must be swallowed to start the replacement"))
            return nullptr;
        size_t backspaces = 0;
        if (!receiveBackspaceRequest(listener, backspaces, "the Telex replacement request did not arrive"))
            return nullptr;
        if (!sendKey(engine, entry, *context, FcitxKey_i, true, "the buffered key must be swallowed while the replacement runs") || !context->commits().empty()) {
            reportFailure("buffer key i before the deletion completes", "no commit during the deletion", "commits=" + std::to_string(context->commits().size()),
                          "the buffered key was committed before the replacement finished");
            return nullptr;
        }
        for (size_t i = 0; i < backspaces; ++i) {
            if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the burst backspace must echo to the app so it deletes the old word"))
                return nullptr;
        }
        if (!sendKey(engine, entry, *context, FcitxKey_Pause, true, "the anchor must be swallowed to finish the replacement"))
            return nullptr;
        return context;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-merged-replacement-commit");
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

    const std::vector<std::string> expected{"ôi"};

    // Phase A: the ibus frontend (gnome-shell, text-input-v3) receives the replaced word and the
    // replayed key as a single commit.
    auto ibusContext = runReplacementWithBufferedKey(testInstance, engine, listener, "ibus");
    if (ibusContext == nullptr)
        return 1;
    if (ibusContext->commits() != expected) {
        reportFailure("merges the replacement and the replayed keys into one commit", "one commit of the whole replacement",
                      "commits=" + std::to_string(ibusContext->commits().size()), "a second commit before the next done event would replace the first one");
        return 1;
    }

    // Phase B: the dbus frontend still defers the whole batch out of the key event.
    auto dbusContext = runReplacementWithBufferedKey(testInstance, engine, listener, "dbus");
    if (dbusContext == nullptr)
        return 1;
    if (!dbusContext->commits().empty()) {
        reportFailure("dbus commits deferred out of the key event", "no commit while the anchor backspace is consumed", "commits=" + std::to_string(dbusContext->commits().size()),
                      "a dbus frontend loses the text attached to the consumed backspace");
        return 1;
    }
    pumpEventLoop(testInstance);
    if (dbusContext->commits() != expected) {
        reportFailure("deferred batch delivered as one commit", "one commit of the whole replacement", "commits=" + std::to_string(dbusContext->commits().size()),
                      "the deferred batch must reach the client as a single commit");
        return 1;
    }

    std::printf("lotus merged replacement commit: all checks passed\n");
    return 0;
}
