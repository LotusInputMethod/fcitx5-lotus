// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine-fixture.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/utf8.h>
#include <memory>
#include <string>

int main() {
    configureTestPaths("fcitx5-lotus-preedit-server");
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
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    for (auto sym : {FcitxKey_a, FcitxKey_s}) {
        if (!sendKey(engine, entry, *context, sym, true, "composition keys must be swallowed into the server preedit")) {
            return 1;
        }
    }
    const auto& preedit = context->inputPanel().preedit();
    if (preedit.toString() != "á" || !fcitx::utf8::validate(preedit.toString()) || preedit.cursor() != 2 || context->preeditUpdates() != 0 || !context->commits().empty() ||
        !context->inputPanel().clientPreedit().toString().empty()) {
        const std::string actual = "client='" + context->inputPanel().clientPreedit().toString() + "' server='" + preedit.toString() +
            "' cursor=" + std::to_string(preedit.cursor()) + " updates=" + std::to_string(context->preeditUpdates()) + " commits=" + std::to_string(context->commits().size());
        reportFailure("compose 'as' into the server preedit", "server=á, cursor=2, no client preedit, no updates, no commits", actual,
                      "without the Preedit capability the composition must render in the server preedit only");
        return 1;
    }
    return 0;
}
