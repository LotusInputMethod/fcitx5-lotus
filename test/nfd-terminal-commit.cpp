// SPDX-License-Identifier: GPL-3.0-or-later
//
// A terminal (xterm.js) does not compose a combining mark that arrives in its own commit, and it
// inserts the raw key when the input method lets it through. With InputMethod=VNI and
// OutputCharset=Unicode tổ hợp, typing "co1" must still leave "có" in the client: the base and its
// tone mark arrive together, in one replacement, and the tone key never reaches the application.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"
#include "utf8.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <fcitx-utils/keysym.h>

namespace {

    std::string codePoints(std::u32string_view text) {
        std::string          out;
        std::array<char, 16> buffer = {};
        for (const char32_t codePoint : text) {
            std::snprintf(buffer.data(), buffer.size(), "U+%04X ", static_cast<unsigned int>(codePoint));
            out += buffer.data();
        }
        return out.empty() ? "<empty>" : out;
    }

    // Runs the fcitx event loop long enough for the post-delay replacement timers to fire.
    void pumpEventLoop(TestInstance& testInstance) {
        auto&      loop      = testInstance.instance.eventLoop();
        const auto exitTimer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 5000, 0, [&loop](fcitx::EventSourceTime*, uint64_t) {
            loop.exit();
            return false;
        });
        loop.exec();
    }

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())), "the engine accepted or forwarded a key unexpectedly");
            return false;
        }
        return true;
    }

    // Appends what a client would show for a forwarded key.
    void appendForwardedKey(std::u32string& text, const std::vector<fcitx::ForwardKeyEvent>& forwarded, size_t from) {
        for (size_t i = from; i < forwarded.size(); ++i) {
            const uint32_t codePoint = fcitx::Key::keySymToUnicode(forwarded[i].key().sym());
            if (codePoint != 0U) {
                text.push_back(static_cast<char32_t>(codePoint));
            }
        }
    }

    // A BackSpace removes one grapheme cluster, never a lone combining mark.
    void deleteClusters(std::u32string& text, size_t count) {
        for (size_t i = 0; i < count && !text.empty(); ++i) {
            text.pop_back();
            while (!text.empty() && isCombiningMark(text.back())) {
                text.pop_back();
            }
        }
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-nfd-terminal");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "VNI");
    config.setValueByPath("OutputCharset", "Unicode tổ hợp");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != fcitx::InputMethodId::Vni ||
        engine.config().outputCharset.value() != fcitx::Charset::UnicodeCombining) {
        reportFailure("configure Smooth/VNI/Unicode tổ hợp", "mode=Smooth, input method=VNI, charset=Unicode tổ hợp", "configuration differs",
                      "the test cannot exercise the decomposed VNI path");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid()) {
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    // A terminal advertises neither preedit nor surrounding text.
    context->setCapabilityFlags(fcitx::CapabilityFlags(0));
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    std::u32string      clientText;
    std::vector<size_t> backspaceCounts;
    size_t              forwardedSeen = 0;
    size_t              commitsSeen   = 0;

    // Appends everything the client would receive: forwarded keys and committed text.
    const auto syncClientText = [&] {
        appendForwardedKey(clientText, context->forwarded(), forwardedSeen);
        forwardedSeen = context->forwarded().size();
        for (size_t i = commitsSeen; i < context->commits().size(); ++i) {
            clientText += lotus::utf8::decode(context->commits()[i]);
        }
        commitsSeen = context->commits().size();
    };

    const auto typeKey = [&](fcitx::KeySym symbol, bool requireAccepted) {
        const size_t forwardedBefore = context->forwarded().size();
        if (!send(engine, entry, *context, symbol, requireAccepted)) {
            return false;
        }
        const bool forwardedNow = context->forwarded().size() != forwardedBefore;
        syncClientText();
        // A key that is neither consumed nor forwarded is delivered by the frontend as typed.
        if (!forwardedNow && !requireAccepted) {
            const uint32_t codePoint = fcitx::Key::keySymToUnicode(symbol);
            if (codePoint != 0U) {
                clientText.push_back(static_cast<char32_t>(codePoint));
            }
        }
        return true;
    };

    const auto drainReplacements = [&] {
        KbMsg msg{};
        while (listener.tryReceive(msg)) {
            if (msg.op != KbOp::Backspace || msg.count <= 0) {
                reportFailure("read replacement request", "op=backspace, count > 0", "op=" + std::to_string(static_cast<int>(msg.op)), "unexpected request kind");
                return false;
            }
            backspaceCounts.push_back(msg.count);
            for (size_t i = 0; i < msg.count; ++i) {
                if (!send(engine, entry, *context, FcitxKey_BackSpace, false)) {
                    return false;
                }
            }
            if (!send(engine, entry, *context, FcitxKey_Pause, true)) {
                return false;
            }
            pumpEventLoop(testInstance);
            // The client deletes first, then receives the replacement text.
            deleteClusters(clientText, msg.count);
            syncClientText();
        }
        return true;
    };

    if (!typeKey(FcitxKey_c, false) || !drainReplacements()) {
        return 1;
    }
    if (!typeKey(FcitxKey_o, false) || !drainReplacements()) {
        return 1;
    }
    if (!typeKey(FcitxKey_1, true) || !drainReplacements()) {
        return 1;
    }

    std::string commitHex;
    for (const auto& commit : context->commits()) {
        commitHex += "[" + codePoints(lotus::utf8::decode(commit)) + "]";
    }
    std::printf("trace: commits=%s forwarded=%zu backspaces=", commitHex.c_str(), context->forwarded().size());
    for (const size_t count : backspaceCounts) {
        std::printf("%zu ", count);
    }
    std::printf("client=[%s]\n", codePoints(clientText).c_str());

    int failures = 0;
    for (const auto& commit : context->commits()) {
        const std::u32string text = lotus::utf8::decode(commit);
        const bool           bare = !text.empty() && std::ranges::all_of(text, [](char32_t codePoint) { return isCombiningMark(codePoint); });
        if (bare) {
            reportFailure("commit carries a bare combining mark", "every commit contains its base character", codePoints(text),
                          "a terminal cannot compose a mark that arrives on its own");
            ++failures;
        }
    }
    const std::u32string expected = U"co\u0301";
    if (clientText != expected) {
        reportFailure("client text after [co1]", codePoints(expected), codePoints(clientText), "the terminal must show the composed word, not the raw keys");
        ++failures;
    }
    for (const auto& forwarded : context->forwarded()) {
        if (forwarded.key().sym() == FcitxKey_1) {
            reportFailure("tone key reaches the client", "the VNI tone key is consumed", "FcitxKey_1 was forwarded", "a forwarded tone key types the raw digit into the terminal");
            ++failures;
            break;
        }
    }

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("nfd terminal commit: all checks passed\n");
    return 0;
}
