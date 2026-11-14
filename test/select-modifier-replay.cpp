// SPDX-License-Identifier: GPL-3.0-or-later
//
// In Select mode the uinput server holds a synthetic Shift for its Shift+Left selection, so a key
// typed during the replacement carries a Shift bit nobody pressed. The modifier must be decided when
// the key is replayed: strip a Shift that is no longer physically held, keep one the user still holds.
#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

    std::string commitText(const TestInputContext& context) {
        std::string text;
        for (const auto& commit : context.commits())
            text += commit;
        return text;
    }

    // One Select replacement of "a" -> "á" with `key` buffered after the single Left echo, i.e. in
    // the window where the old push-time rule kept the synthetic Shift. `holdShift` presses the
    // physical Shift while the replacement is in flight, which must keep the capital.
    bool runCase(fcitx::Instance* instance, fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, KbSocketListener& listener, bool holdShift,
                 const std::string& expected) {
        auto context = std::make_unique<TestInputContext>(instance);
        context->focusIn();
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);

        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_a), false, "plain letters before the trigger must echo to the app") ||
            !sendKey(engine, entry, *context, fcitx::Key(FcitxKey_s), true, "the trigger key must be swallowed to start the replacement"))
            return false;

        KbMsg msg{};
        if (!listener.receive(msg, "the Telex replacement request did not arrive"))
            return false;
        if (msg.op != KbOp::Select || msg.count != 1) {
            reportFailure("receive replacement request", "op=select, count=1", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                          "the Select replacement did not start as expected");
            return false;
        }
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Left), false, "the selection echo passes through while the replacement is still deleting")) {
            return false;
        }
        if (holdShift && !sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Shift_L), true, "the physical Shift must be swallowed while the replacement deletes")) {
            return false;
        }
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_N, fcitx::KeyState::Shift), true, "a key typed during the replacement must be swallowed and buffered")) {
            return false;
        }
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Delete), false, "the Delete echo must reach the application")) {
            return false;
        }
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Pause), true, "the anchor must be swallowed to finish the replacement")) {
            return false;
        }

        const std::string actual = commitText(*context);
        if (actual != expected) {
            reportFailure(holdShift ? "replay with the physical Shift held" : "replay with only the synthetic Shift", expected, "'" + actual + "'",
                          "the buffered key was replayed with the wrong Shift decision");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-modifier-replay");
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

    // Case 1: only the server's synthetic Shift is down -> the replayed 'N' must stay lowercase.
    if (!runCase(&testInstance.instance, engine, entry, listener, false, "án"))
        return 1;

    // Case 2: the user really holds Shift -> the capital must survive the replay.
    if (!runCase(&testInstance.instance, engine, entry, listener, true, "áN"))
        return 1;

    std::printf("lotus select modifier replay: all checks passed\n");
    return 0;
}
