// SPDX-FileCopyrightText: 2026 Duc Vu <ducvd89@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Types whole words in Surrounding Text mode against a simulated client text: every commit and
// deleteSurroundingText is applied to the text, and a key the engine lets through is appended as
// the client would. Guards the W2U undo: "ww" must leave "w", not "ưw".
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/utf8.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

    struct Typer {
        fcitx::LotusEngine&      engine;
        fcitx::InputMethodEntry& entry;
        TestInputContext&        context;
        std::string              text;

        void                     typeKey(char ch) {
            const size_t cursor = fcitx_utf8_strlen(text.c_str());
            context.surroundingText().setText(text, cursor, cursor);
            context.updateSurroundingText();
            const size_t    commitsBefore = context.commits().size();
            const size_t    deletesBefore = context.deletes().size();

            fcitx::KeyEvent event(&context, fcitx::Key(static_cast<fcitx::KeySym>(ch)), false);
            engine.keyEvent(entry, event);

            for (size_t i = deletesBefore; i < context.deletes().size(); ++i) {
                const auto [offset, size] = context.deletes()[i];
                const size_t length       = fcitx_utf8_strlen(text.c_str());
                const size_t start        = length + offset;
                const char*  begin        = fcitx_utf8_get_nth_char(text.c_str(), start);
                const char*  end          = fcitx_utf8_get_nth_char(begin, size);
                text.erase(static_cast<size_t>(begin - text.c_str()), static_cast<size_t>(end - begin));
            }
            for (size_t i = commitsBefore; i < context.commits().size(); ++i) {
                text += context.commits()[i];
            }
            if (!event.accepted()) {
                text += ch;
            }
        }

        std::string type(std::string_view keys) {
            text.clear();
            fcitx::InputContextEvent resetEvent(&context, fcitx::EventType::InputContextReset);
            engine.reset(entry, resetEvent);
            for (const char ch : keys) {
                typeKey(ch);
            }
            return text;
        }
    };

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-surrounding-text-typing");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Surrounding Text");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("W2U", "Everywhere");
    config.setValueByPath("EnableMacro", "False");
    engine.setConfig(config);

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    struct Case {
        std::string_view keys;
        std::string_view want;
    };
    constexpr Case cases[] = {
        {"w", "ư"}, {"ww", "w"}, {"tw", "tư"}, {"tww", "tw"}, {"tuwf", "từ"}, {"duwowng", "dương"}, {"cuwngs", "cứng"}, {"vieetj", "việt"}, {"tieengs", "tiếng"}, {"abc.", "abc."},
    };

    Typer typer{engine, entry, *context, {}};
    int   failures = 0;
    for (const Case& test : cases) {
        const std::string got = typer.type(test.keys);
        if (got != test.want) {
            std::cerr << "Surrounding Text typing '" << test.keys << "': expected '" << test.want << "', got '" << got << "'\n";
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
