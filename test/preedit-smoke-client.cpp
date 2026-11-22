// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine-fixture.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/utf8.h>
#include <memory>
#include <string>

int main() {
    configureTestPaths("fcitx5-lotus-preedit-client");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Preedit || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex) {
        reportFailure("configure Preedit/Telex", "mode=Preedit, inputMethod=Telex", "the configuration did not load",
                      "the test cannot exercise the preedit path without Preedit/Telex");
        return 1;
    }
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    // Bamboo Telex processes a then s as U+00E1 LATIN SMALL LETTER A WITH ACUTE.
    for (auto sym : {FcitxKey_a, FcitxKey_s}) {
        if (!sendKey(engine, entry, *context, sym, true, "composition keys must be swallowed into the client preedit")) {
            return 1;
        }
    }
    const auto& preedit = context->inputPanel().clientPreedit();
    if (preedit.toString() != "á" || !fcitx::utf8::validate(preedit.toString()) || preedit.cursor() != 2 || context->preeditUpdates() != 2 || !context->commits().empty() ||
        !context->inputPanel().preedit().toString().empty()) {
        const std::string actual = "client='" + preedit.toString() + "' server='" + context->inputPanel().preedit().toString() + "' cursor=" + std::to_string(preedit.cursor()) +
            " updates=" + std::to_string(context->preeditUpdates()) + " commits=" + std::to_string(context->commits().size());
        reportFailure("compose 'as' into the client preedit", "client=á, cursor=2, updates=2, no commits, empty server preedit", actual,
                      "the client preedit must carry the composed word while the server preedit stays empty");
        return 1;
    }
    return 0;
}
