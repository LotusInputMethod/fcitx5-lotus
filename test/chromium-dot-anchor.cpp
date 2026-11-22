// SPDX-License-Identifier: GPL-3.0-or-later
//
// Chromium drops a text replacement unless a character was just typed and deleted: the addon commits
// the anchor U+00B7 before the burst and deletes one extra character. Every other application keeps
// the plain backspace burst.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"
#include "utf8.h"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fcitx-utils/keysym.h>

namespace {

    constexpr std::u32string_view kAnchor       = U"\u00B7";
    constexpr std::u32string_view kExpectedWord = U"\u00E1"; // á

    int                           failures = 0;

    void                          check(const char* name, bool condition, std::string_view detail) {
        if (condition) {
            return;
        }
        std::printf("FAIL %s: %s\n", name, std::string(detail).c_str());
        ++failures;
    }

    std::string hexText(std::u32string_view text) {
        std::string          out;
        std::array<char, 16> buffer = {};
        for (const char32_t codePoint : text) {
            std::snprintf(buffer.data(), buffer.size(), "U+%04X ", static_cast<unsigned int>(codePoint));
            out += buffer.data();
        }
        return out.empty() ? "<empty>" : out;
    }

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            std::printf("FAIL process key %u: accepted=%d expected=%d\n", static_cast<unsigned int>(symbol), static_cast<int>(event.accepted()), static_cast<int>(requireAccepted));
            ++failures;
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-chromium-dot-anchor");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        std::printf("FAIL configure Smooth/Telex\n");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid()) {
        return 1;
    }

    // "a" then "s" in Telex composes "á", so the engine replaces the single character.
    const auto runApp = [&](const char* program, bool expectAnchor) {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance, program);
        context->focusIn();
        fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);

        if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true)) {
            return;
        }

        KbMsg msg{};
        if (!listener.receive(msg, "the Telex replacement request did not arrive")) {
            ++failures;
            return;
        }
        const size_t expectedCount = expectAnchor ? 2U : 1U;
        check("replacement request op", msg.op == KbOp::Backspace, "op=" + std::to_string(static_cast<int>(msg.op)));
        check("replacement request count", msg.count == expectedCount, "count=" + std::to_string(msg.count) + " expected=" + std::to_string(expectedCount));

        size_t anchorCommits = 0;
        for (const auto& commit : context->commits()) {
            const std::u32string text = lotus::utf8::decode(commit);
            if (text == kAnchor) {
                ++anchorCommits;
            }
        }
        check("anchor is committed before the burst", anchorCommits == (expectAnchor ? 1U : 0U), "anchor commits=" + std::to_string(anchorCommits));

        for (size_t i = 0; i < msg.count; ++i) {
            if (!send(engine, entry, *context, FcitxKey_BackSpace, false)) {
                return;
            }
        }
        if (!send(engine, entry, *context, FcitxKey_Pause, true)) {
            return;
        }

        std::u32string replaced;
        for (const auto& commit : context->commits()) {
            const std::u32string text = lotus::utf8::decode(commit);
            if (text != kAnchor) {
                replaced += text;
            }
        }
        check("replacement text", replaced == kExpectedWord, "text=[" + hexText(replaced) + "]");
    };

    runApp("chrome", true);
    runApp("gedit", false);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("chromium dot anchor: all checks passed\n");
    return 0;
}
