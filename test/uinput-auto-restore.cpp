// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file uinput-auto-restore.cpp
 * @brief Types English words in a uinput mode against a simulated client buffer.
 *
 * The automatic non-Vietnamese restore must rewrite the word to the raw key strokes while it is typed, the
 * same way the Preedit mode does. Every injected backspace burst is applied to the simulated buffer, and a
 * key the engine lets through is appended as the client would.
 */

#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"
#include "utf8.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace {

    using lotus::utf8::decode;
    using lotus::utf8::encode;

    bool sendKey(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "the uinput auto restore test saw an unexpected key outcome");
            return false;
        }
        return true;
    }

    bool completeOneBurst(const KbMsg& msg, KbSocketListener& listener, fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context,
                          std::u32string& text, std::vector<std::string>& trace) {
        (void)listener;
        const bool isSelect = msg.op == KbOp::Select;
        if ((!isSelect && msg.op != KbOp::Backspace) || msg.count == 0 || msg.count > text.size()) {
            reportFailure("receive replacement request", "op=backspace|select, count <= word length",
                          "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count) + ", word length=" + std::to_string(text.size()),
                          "the replacement request does not match the typed word");
            return false;
        }
        trace.push_back(std::string(isSelect ? "select:" : "burst:") + std::to_string(msg.count) + " (word " + encode(text) + ")");
        text.resize(text.size() - msg.count);
        const fcitx::KeySym injected = isSelect ? FcitxKey_Left : FcitxKey_BackSpace;
        for (size_t i = 0; i < msg.count; ++i) {
            if (!sendKey(engine, entry, context, injected, false)) {
                return false;
            }
        }
        if (isSelect && !sendKey(engine, entry, context, FcitxKey_Delete, false)) {
            return false;
        }
        return sendKey(engine, entry, context, FcitxKey_Pause, true);
    }

    bool completeBurst(KbSocketListener& listener, fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, std::u32string& text,
                       std::vector<std::string>& trace) {
        KbMsg msg{};
        while (listener.tryReceive(msg)) {
            if (!completeOneBurst(msg, listener, engine, entry, context, text, trace)) {
                return false;
            }
        }
        return true;
    }

    bool typeWord(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, KbSocketListener& listener, std::string_view keys,
                  std::u32string& text, std::vector<std::string>& trace, bool clearFirst = true) {
        if (clearFirst) {
            text.clear();
            fcitx::InputContextEvent resetEvent(&context, fcitx::EventType::InputContextReset);
            engine.reset(entry, resetEvent);
        }
        KbMsg drained{};
        while (listener.tryReceive(drained)) {}

        for (const char key : keys) {
            const size_t    commitsBefore = context.commits().size();

            fcitx::KeyEvent event(&context, fcitx::Key(static_cast<fcitx::KeySym>(static_cast<unsigned char>(key))), false);
            engine.keyEvent(entry, event);

            if (!completeBurst(listener, engine, entry, context, text, trace)) {
                return false;
            }
            for (size_t i = commitsBefore; i < context.commits().size(); ++i) {
                trace.push_back("commit:" + context.commits()[i]);
                text += decode(context.commits()[i]);
            }
            if (!event.accepted()) {
                trace.push_back(std::string("fwd:") + key);
                text.push_back(static_cast<char32_t>(key));
            }
        }
        return true;
    }

    bool typeWordDeferred(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, KbSocketListener& listener, std::string_view keys,
                          std::u32string& text, std::vector<std::string>& trace) {
        text.clear();
        fcitx::InputContextEvent resetEvent(&context, fcitx::EventType::InputContextReset);
        engine.reset(entry, resetEvent);
        std::vector<KbMsg> pending;
        KbMsg              drained{};
        while (listener.tryReceive(drained)) {}

        for (const char key : keys) {
            const size_t    commitsBefore = context.commits().size();

            fcitx::KeyEvent event(&context, fcitx::Key(static_cast<fcitx::KeySym>(static_cast<unsigned char>(key))), false);
            engine.keyEvent(entry, event);

            KbMsg msg{};
            while (listener.tryReceive(msg)) {
                pending.push_back(msg);
            }
            for (size_t i = commitsBefore; i < context.commits().size(); ++i) {
                trace.push_back("commit:" + context.commits()[i]);
                text += decode(context.commits()[i]);
            }
            if (!event.accepted() && pending.empty()) {
                trace.push_back(std::string("fwd:") + key);
                text.push_back(static_cast<char32_t>(key));
            } else if (!event.accepted()) {
                trace.push_back(std::string("buffered:") + key);
            }
        }

        for (int guard = 0; guard < 100 && !pending.empty(); ++guard) {
            const KbMsg  burst         = pending.front();
            const size_t commitsBefore = context.commits().size();
            pending.erase(pending.begin());
            if (!completeOneBurst(burst, listener, engine, entry, context, text, trace)) {
                return false;
            }
            for (size_t i = commitsBefore; i < context.commits().size(); ++i) {
                trace.push_back("commit:" + context.commits()[i]);
                text += decode(context.commits()[i]);
            }
            KbMsg msg{};
            while (listener.tryReceive(msg)) {
                pending.push_back(msg);
            }
        }
        return true;
    }
} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-uinput-auto-restore");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    KbSocketListener   listener;
    if (!listener.valid())
        return 1;

    const fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    struct Case {
        std::string_view    keys;
        std::u32string_view want;
    };

    constexpr std::array<Case, 2> cases{
        Case{.keys = "receiver", .want = U"receiver"},
        Case{.keys = "restore", .want = U"restore"},
    };

    int failures = 0;
    for (const char* mode : {"Uinput (Smooth)", "Uinput (Select)"}) {
        fcitx::RawConfig config;
        config.setValueByPath("Mode", mode);
        config.setValueByPath("InputMethod", "Telex");
        config.setValueByPath("DefaultBackspaceInterval", "1");
        config.setValueByPath("AutoNonVnRestore", "True");
        config.setValueByPath("EnableMacro", "False");
        engine.setConfig(config);

        for (const Case& test : cases) {
            for (const bool deferred : {false, true}) {
                auto ctx = std::make_unique<TestInputContext>(&testInstance.instance);
                ctx->focusIn();
                fcitx::InputContextEvent focusEvent(ctx.get(), fcitx::EventType::InputContextFocusIn);
                engine.activate(entry, focusEvent);

                std::u32string           text;
                std::vector<std::string> trace;
                const bool               okay =
                    deferred ? typeWordDeferred(engine, entry, *ctx, listener, test.keys, text, trace) : typeWord(engine, entry, *ctx, listener, test.keys, text, trace);
                if (!okay) {
                    return 1;
                }
                if (text != test.want) {
                    std::printf("FAIL uinput auto restore '%s' in '%s' (%s): expected '%s', got '%s'\n", std::string(test.keys).c_str(), mode, deferred ? "deferred" : "immediate",
                                encode(test.want).c_str(), encode(text).c_str());
                    for (const std::string& step : trace) {
                        std::printf("  %s\n", step.c_str());
                    }
                    ++failures;
                }
            }
        }
    }

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus uinput auto restore: all checks passed\n");
    return 0;
}
