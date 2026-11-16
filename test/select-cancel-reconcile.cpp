// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Select replacement aborted at focus out must be committed once focus
// returns: the uinput server finishes the selection on Cancel, so the old text
// is always fully removed and the stashed replacement text belongs at the
// cursor. Escape triggers the commit without feeding the word engine.
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

    // Types "at" + the sac mark key, waits for the Select request of 2 characters.
    bool startAtSacReplacement(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, KbSocketListener& listener, const char* meaning) {
        if (!sendKey(engine, entry, context, FcitxKey_a, false) || !sendKey(engine, entry, context, FcitxKey_t, false) || !sendKey(engine, entry, context, FcitxKey_s, true))
            return false;
        KbMsg msg{};
        if (!listener.receive(msg, meaning))
            return false;
        if (msg.op == KbOp::Select && msg.count == 2)
            return true;
        reportFailure("receive replacement request", "op=select, count=2", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count), meaning);
        return false;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-cancel-reconcile");
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

    // Scenario 1: the burst is aborted by a focus bounce. The server finishes
    // the selection, so the first key after the bounce commits the stashed
    // replacement text as is.
    {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        gainFocus(engine, entry, *context);
        if (!startAtSacReplacement(engine, entry, *context, listener, "scenario 1: the select request did not arrive"))
            return 1;
        loseFocus(engine, entry, *context);
        KbMsg msg{};
        if (listener.tryReceive(msg)) {
            reportFailure("no cancel while the focus may bounce", "no request at focus-out time", "op=" + std::to_string(static_cast<int>(msg.op)),
                          "the abort must wait for a possible focus bounce");
            return 1;
        }
        auto other = std::make_unique<TestInputContext>(&testInstance.instance);
        gainFocus(engine, entry, *other);
        if (!listener.receive(msg, "scenario 1: the cancel request did not arrive"))
            return 1;
        if (msg.op != KbOp::Cancel) {
            reportFailure("receive cancel request", "op=cancel", "op=" + std::to_string(static_cast<int>(msg.op)), "scenario 1: focus out must cancel the burst");
            return 1;
        }
        if (!context->commits().empty()) {
            reportFailure("verify no commit at focus out", "no commits", commitList(*context), "scenario 1: the aborted replacement must not commit while unfocused");
            return 1;
        }
        gainFocus(engine, entry, *context);
        if (!sendKey(engine, entry, *context, FcitxKey_Escape, false))
            return 1;
        const std::vector<std::string> stashed{"át"};
        if (context->commits() != stashed) {
            reportFailure("verify commit after the bounce", "commits=['át']", commitList(*context), "scenario 1: the stashed replacement was not committed once focus returned");
            return 1;
        }
    }

    // Scenario 2: the context stays away past the stale window, likely a real
    // application switch. The stash is dropped and nothing is committed.
    {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        gainFocus(engine, entry, *context);
        if (!startAtSacReplacement(engine, entry, *context, listener, "scenario 2: the select request did not arrive"))
            return 1;
        loseFocus(engine, entry, *context);
        KbMsg msg{};
        if (listener.tryReceive(msg)) {
            reportFailure("no cancel while the focus may bounce", "no request at focus-out time", "op=" + std::to_string(static_cast<int>(msg.op)),
                          "the abort must wait for a possible focus bounce");
            return 1;
        }
        auto other = std::make_unique<TestInputContext>(&testInstance.instance);
        gainFocus(engine, entry, *other);
        if (!listener.receive(msg, "scenario 2: the cancel request did not arrive"))
            return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        gainFocus(engine, entry, *context);
        if (!sendKey(engine, entry, *context, FcitxKey_Escape, false))
            return 1;
        if (!context->commits().empty()) {
            reportFailure("verify no commit for the stale stash", "no commits", commitList(*context), "scenario 2: the stale stash must not commit");
            return 1;
        }
        KbMsg msg2{};
        if (listener.tryReceive(msg2)) {
            reportFailure("wait after the stale stash", "no replacement request", "a replacement request arrived", "scenario 2: the stale stash must not re-run a replacement");
            return 1;
        }
    }

    // Scenario 3: typing resumes normally after a committed abort; the next
    // word triggers a fresh replacement that completes through its echoes.
    {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        gainFocus(engine, entry, *context);
        if (!startAtSacReplacement(engine, entry, *context, listener, "scenario 3: the select request did not arrive"))
            return 1;
        loseFocus(engine, entry, *context);
        KbMsg msg{};
        if (listener.tryReceive(msg)) {
            reportFailure("no cancel while the focus may bounce", "no request at focus-out time", "op=" + std::to_string(static_cast<int>(msg.op)),
                          "the abort must wait for a possible focus bounce");
            return 1;
        }
        auto other = std::make_unique<TestInputContext>(&testInstance.instance);
        gainFocus(engine, entry, *other);
        if (!listener.receive(msg, "scenario 3: the cancel request did not arrive"))
            return 1;
        gainFocus(engine, entry, *context);
        if (!sendKey(engine, entry, *context, FcitxKey_Escape, false))
            return 1;
        const std::vector<std::string> stashed{"át"};
        if (context->commits() != stashed) {
            reportFailure("verify commit of the aborted replacement", "commits=['át']", commitList(*context), "scenario 3: the aborted replacement was not committed");
            return 1;
        }
        if (!startAtSacReplacement(engine, entry, *context, listener, "scenario 3: the fresh select request did not arrive"))
            return 1;
        if (!sendKey(engine, entry, *context, FcitxKey_Left, false) || !sendKey(engine, entry, *context, FcitxKey_Left, false) ||
            !sendKey(engine, entry, *context, FcitxKey_Pause, true))
            return 1;
        const std::vector<std::string> fresh{"át", "át"};
        if (context->commits() != fresh) {
            reportFailure("verify commit of the fresh replacement", "commits=['át']['át']", commitList(*context), "scenario 3: the fresh replacement did not commit normally");
            return 1;
        }
    }

    std::printf("lotus select cancel reconcile: all checks passed\n");
    return 0;
}
