// SPDX-License-Identifier: GPL-3.0-or-later
//
// Chromium's address bar autocompletes inline. A BackSpace burst spends its first key collapsing that
// selection, so Lotus delivers the burst with Shift+Left instead. Everything that follows must be
// driven by the burst that was sent, not by the configured mode:
//   - the Delete echo has to reach the application, otherwise the suggestion survives (dđ);
//   - a key buffered during the burst carries the synthetic Shift, so the replay must strip it as
//     long as the physical Shift is not held.
#include "engine-fixture.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

    std::string commitText(const TestInputContext& context) {
        std::string text;
        for (const auto& commit : context.commits())
            text += commit;
        return text;
    }

    // Types "d" + "d" on the address bar and completes the Select burst. When `bufferedKey` is not
    // empty it is typed while the burst runs and must be replayed after the commit.
    bool runAddressBarReplacement(fcitx::Instance* instance, fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, KbSocketListener& listener,
                                  std::optional<fcitx::Key> bufferedKey) {
        auto context = std::make_unique<TestInputContext>(instance, "chromium");
        context->setCapabilityFlags(fcitx::CapabilityFlag::Url);
        context->focusIn();
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);

        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_d), false))
            return false;
        context->surroundingText().setText("docs.google.com", 1, 16);
        context->updateSurroundingText();
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_d), true))
            return false;

        KbMsg msg{};
        if (!listener.receive(msg, "the address bar replacement did not start"))
            return false;
        if (msg.op != KbOp::Select) {
            reportFailure("address bar burst", "op=select", "op=" + std::to_string(static_cast<int>(msg.op)), "the address bar must use the selection path");
            return false;
        }
        for (size_t i = 0; i < msg.count; ++i) {
            if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Left), false))
                return false;
        }
        if (bufferedKey.has_value() && !sendKey(engine, entry, *context, *bufferedKey, true))
            return false;
        // The Delete echo must reach the application: the addon leaves the event unaccepted (the app
        // drops the autocomplete selection); a swallowed echo means the old character survives (dđ).
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Delete), false)) {
            reportFailure("Delete echo reaches the application", "accepted=false", "accepted=true",
                          "the app could not drop the autocomplete selection, so the old character survives (dđ)");
            return false;
        }
        if (!sendKey(engine, entry, *context, fcitx::Key(FcitxKey_Pause), true))
            return false;

        const std::string expected = bufferedKey.has_value() ? "đn" : "đ";
        if (commitText(*context) != expected) {
            reportFailure(bufferedKey.has_value() ? "buffered key replayed without the synthetic Shift" : "commit after the address bar replacement", expected,
                          commitText(*context), "the address bar replacement produced the wrong text");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-chromium-address-bar");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    if (!runAddressBarReplacement(&testInstance.instance, engine, entry, listener, std::nullopt))
        return 1;
    // A key typed during the burst is replayed with the synthetic Shift stripped.
    if (!runAddressBarReplacement(&testInstance.instance, engine, entry, listener, fcitx::Key(FcitxKey_N, fcitx::KeyState::Shift)))
        return 1;

    std::printf("lotus chromium address bar: all checks passed\n");
    return 0;
}
