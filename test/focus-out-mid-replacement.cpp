// SPDX-License-Identifier: GPL-3.0-or-later
//
// A focus bounce in the middle of a replacement loses the backspace echo for the injected keys: the
// input context is unfocused, so fcitx never delivers those key events back to the addon. The state
// must not stay in the deleting state forever - every further key would be queued and dropped.
#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main() {
    configureTestPaths("fcitx5-lotus-focus-out-mid-replacement");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DefaultBackspaceInterval", "10");
    engine.setConfig(config);

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    context = std::make_unique<TestInputContext>(&testInstance.instance);
    gainFocus(engine, entry, *context);

    // a, n, g then f: Telex adds the huyền tone, so the engine replaces "ng" of "ang" with "àng".
    if (!sendKey(engine, entry, *context, FcitxKey_a, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_n, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_g, false, "plain letters before the trigger must echo to the app") ||
        !sendKey(engine, entry, *context, FcitxKey_f, true, "the tone trigger must be swallowed to start the replacement"))
        return 1;
    KbMsg msg{};
    if (!listener.receive(msg, "the Telex replacement request did not arrive"))
        return 1;
    if (msg.op != KbOp::Backspace || msg.count != 3) {
        reportFailure("receive replacement request", "op=backspace, count=3", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                      "the replacement never started");
        return 1;
    }

    // Only the first echo of the three arrives; the rest are lost while the context is unfocused.
    if (!sendKey(engine, entry, *context, FcitxKey_BackSpace, false, "the first burst backspace must echo to the app before the focus bounces"))
        return 1;

    // Focus bounce in the middle of the deleting state.
    loseFocus(engine, entry, *context);
    gainFocus(engine, entry, *context);

    // Keys typed while the ack never completes must be queued, not committed.
    if (!sendKey(engine, entry, *context, FcitxKey_q, true, "a key typed while the echo is lost must be swallowed and queued"))
        return 1;
    if (!context->commits().empty()) {
        reportFailure("type while the replacement is pending", "no commits yet", "commits=" + std::to_string(context->commits().size()),
                      "a key was committed while the replacement still waits for its backspace echo");
        return 1;
    }

    // Past the ack deadline the replacement must finish and the queued key must be replayed. The
    // next activation is what checks the deadline.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    gainFocus(engine, entry, *context);

    std::string committed;
    for (const auto& commit : context->commits())
        committed += commit;
    if (committed != "àngq") {
        reportFailure("finish the overdue replacement", "committed text 'àngq' ('àng' + replayed 'q')", "committed='" + committed + "'",
                      "the replacement stayed stuck in the deleting state after the backspace echo was lost");
        return 1;
    }

    // After the recovery the queued key is gone from the queue: a plain letter is forwarded again.
    if (!sendKey(engine, entry, *context, FcitxKey_q, false, "after recovery a plain letter must echo to the app again"))
        return 1;

    std::printf("lotus focus-out mid replacement: all checks passed\n");
    return 0;
}
