// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "test-input-context.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-modifier-key-bypasses-surrounding-text");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Surrounding Text");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::SurroundingText || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Surrounding Text/Telex", "mode=SurroundingText, input method=Telex", "configured mode or input method differs",
                      "the raw Fcitx configuration values did not serialize to Surrounding Text Telex behavior");
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->surroundingText().setText("abcdef", 2, 5);
    context->updateSurroundingText();
    context->resetPreeditUpdateCount();

    fcitx::KeyEvent event(context.get(), fcitx::Key(FcitxKey_Control_L), false);
    engine.keyEvent(entry, event);
    if (event.accepted() || !context->deletes().empty() || !context->commits().empty() || !context->forwarded().empty()) {
        reportFailure("send Control_L press with selected surrounding text", "accepted=false, deletes=0, commits=0, forwarded=0",
                      "accepted=" + std::to_string(event.accepted()) + ", deletes=" + std::to_string(context->deletes().size()) +
                          ", commits=" + std::to_string(context->commits().size()) + ", forwarded=" + std::to_string(context->forwarded().size()),
                      "a modifier-only key must not enter surrounding-text processing");
        return 1;
    }

    return 0;
}
