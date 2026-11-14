// SPDX-License-Identifier: GPL-3.0-or-later
//
// A replacement dropped by the engine must tell the uinput server to stop pacing
// its burst, and keys typed while the burst tail still holds Shift must replay
// without the phantom Shift.
#include "engine-fixture.h"
#include "fcitx-utils/keysym.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

    bool expectMsg(const KbMsg& msg, KbOp op, size_t count, const char* step, const char* meaning) {
        if (msg.op == op && msg.count == count)
            return true;
        reportFailure(step, "op=" + std::to_string(static_cast<int>(op)) + ", count=" + std::to_string(count),
                      "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count), meaning);
        return false;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-cancel");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    context = std::make_unique<TestInputContext>(&testInstance.instance);
    gainFocus(engine, entry, *context);

    // Another context taking the focus is what abandons a burst: a bounce keeps it, a real switch cancels it.
    auto otherContext = std::make_unique<TestInputContext>(&testInstance.instance);

    // A burst abandoned at focus-out is cancelled once the focus lands somewhere else.
    if (!sendKey(engine, entry, *context, FcitxKey_a, false) || !sendKey(engine, entry, *context, FcitxKey_t, false) || !sendKey(engine, entry, *context, FcitxKey_s, true))
        return 1;
    KbMsg msg{};
    if (!listener.receive(msg, "the replacement request did not arrive"))
        return 1;
    if (!expectMsg(msg, KbOp::Select, 2, "receive replacement request", "the replacement never started"))
        return 1;

    loseFocus(engine, entry, *context);
    if (listener.tryReceive(msg)) {
        reportFailure("no cancel while the focus may bounce", "no request at focus-out time", "op=" + std::to_string(static_cast<int>(msg.op)),
                      "the abort must wait for a possible focus bounce");
        return 1;
    }
    gainFocus(engine, entry, *otherContext);
    if (!listener.receive(msg, "the cancel request after the focus moved away did not arrive"))
        return 1;
    if (!expectMsg(msg, KbOp::Cancel, 0, "receive cancel request", "a burst whose context did not come back must be cancelled"))
        return 1;
    if (!context->commits().empty()) {
        reportFailure("verify no commit after the cancel", "no commits", commitList(*context), "the cancelled replacement must commit nothing");
        return 1;
    }

    // Typing resumes right after the abandoned burst: the first key commits the stashed replacement.
    gainFocus(engine, entry, *context);
    if (!sendKey(engine, entry, *context, FcitxKey_Escape, false))
        return 1;
    const std::vector<std::string> afterAbandoned{"át"};
    if (context->commits() != afterAbandoned) {
        reportFailure("verify commit after the abandoned burst", "commits=['át']", commitList(*context), "the stashed replacement was not committed once focus returned");
        return 1;
    }

    // A focus bounce keeps the Select burst alive: no Cancel is sent, the remaining Left echoes finish the
    // selection and the replacement commits exactly once.
    if (!sendKey(engine, entry, *context, FcitxKey_a, false) || !sendKey(engine, entry, *context, FcitxKey_t, false) || !sendKey(engine, entry, *context, FcitxKey_s, true))
        return 1;
    if (!listener.receive(msg, "the replacement request before the bounce did not arrive"))
        return 1;
    if (!expectMsg(msg, KbOp::Select, 2, "receive replacement request", "the replacement before the bounce never started"))
        return 1;
    loseFocus(engine, entry, *context);
    if (listener.tryReceive(msg)) {
        reportFailure("no cancel on a focus bounce", "no request at focus-out time", "op=" + std::to_string(static_cast<int>(msg.op)),
                      "a bouncing focus must keep the burst running");
        return 1;
    }
    gainFocus(engine, entry, *context);
    if (listener.tryReceive(msg)) {
        reportFailure("no request when the focus returns", "no request on refocus", "op=" + std::to_string(static_cast<int>(msg.op)),
                      "the resumed burst must complete through its own echoes");
        return 1;
    }
    if (!sendKey(engine, entry, *context, FcitxKey_Left, false) || !sendKey(engine, entry, *context, FcitxKey_Left, false) ||
        !sendKey(engine, entry, *context, FcitxKey_Pause, true))
        return 1;
    const std::vector<std::string> afterBounce{"át", "át"};
    if (context->commits() != afterBounce) {
        reportFailure("verify commit after the bounce", "commits=['át']['át']", commitList(*context), "the resumed replacement did not commit normally");
        return 1;
    }

    // A burst abandoned past its ack deadline (echoes lost without any focus
    // change) is still cancelled on the next key, and the pending text is
    // committed exactly once. The 'a' re-composes "át" into "ất" (the Telex
    // aa -> â rule) and triggers a fresh replacement; Escape both triggers the
    // overdue check and cleans the word state.
    const size_t commitsBeforeTimeout = context->commits().size();
    if (!sendKey(engine, entry, *context, FcitxKey_a, true))
        return 1;
    if (!listener.receive(msg, "the second replacement request did not arrive"))
        return 1;
    if (!expectMsg(msg, KbOp::Select, 2, "receive replacement request", "the second replacement never started"))
        return 1;

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    if (!sendKey(engine, entry, *context, FcitxKey_Escape, false))
        return 1;
    if (!listener.receive(msg, "the cancel request after the ack timeout did not arrive"))
        return 1;
    if (!expectMsg(msg, KbOp::Cancel, 0, "receive cancel request", "the ack timeout must cancel the abandoned burst"))
        return 1;
    if (context->commits().size() != commitsBeforeTimeout + 1) {
        reportFailure("verify commit after the ack timeout", "one pending commit", commitList(*context), "the abandoned replacement text was not committed exactly once");
        return 1;
    }

    // Escape cleaned the word state, so the next syllable starts from a clean
    // buffer. While a Select burst is in flight the server holds LEFTSHIFT until
    // the burst tail, so keys typed near the tail carry a phantom Shift state
    // that must replay unshifted.
    if (!sendKey(engine, entry, *context, FcitxKey_e, false) || !sendKey(engine, entry, *context, FcitxKey_s, true))
        return 1;
    if (!listener.receive(msg, "the third replacement request did not arrive"))
        return 1;
    if (!expectMsg(msg, KbOp::Select, 1, "receive replacement request", "the third replacement never started"))
        return 1;

    if (!sendKey(engine, entry, *context, FcitxKey_Left, false) || !sendKey(engine, entry, *context, fcitx::Key(FcitxKey_N, fcitx::KeyState::Shift), true) ||
        !sendKey(engine, entry, *context, FcitxKey_Pause, true))
        return 1;
    if (context->commits().empty() || context->commits().back() != "én") {
        reportFailure("verify commit after the phantom Shift", "commits ending with ['én']", commitList(*context), "the phantom Shift was not stripped from the buffered key");
        return 1;
    }

    std::printf("lotus select cancel: all checks passed\n");
    return 0;
}
