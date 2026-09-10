// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (!event.accepted()) {
            reportFailure("send emoji navigation key", "key accepted", "key rejected", "the real Emoji-mode key handler did not handle the navigation key");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-emoji-history-page-navigation");
    const auto    historyPath = std::filesystem::path(std::getenv("XDG_CONFIG_HOME")) / "fcitx5/conf/lotus-emoji-history.conf";
    std::ofstream history(historyPath);
    const char*   emoji[] = {"😀", "😁", "😂", "😃", "😄", "😅", "😆", "😉", "😊", "😋", "😎", "😍", "😘", "😗", "😙", "😚", "🙂", "🤗"};
    for (int i = 0; i < 18; ++i)
        history << "history" << i << '=' << emoji[i] << '\n';
    history.close();

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Emoji Picker");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Emoji) {
        reportFailure("configure Emoji Picker", "mode=Emoji Picker", "configured mode differs", "the test cannot enter the real emoji history mode");
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    auto candidates = std::dynamic_pointer_cast<fcitx::CommonCandidateList>(context->inputPanel().candidateList());
    if (!candidates || candidates->totalSize() != 18 || candidates->pageSize() != 9) {
        reportFailure("load emoji history fixture", "18 candidates with page size 9",
                      candidates ? "candidates=" + std::to_string(candidates->totalSize()) + ", page-size=" + std::to_string(candidates->pageSize()) : "no candidate list",
                      "EmojiLoader::loadHistory did not provide the multi-page fixture to the real emoji mode");
        return 1;
    }

    // Enter page two using the production Page_Down path, then move Up from its
    // first candidate. This must wrap within page two, rather than changing page.
    if (!send(engine, entry, *context, FcitxKey_Page_Down) || !send(engine, entry, *context, FcitxKey_Up))
        return 1;

    if (candidates->currentPage() != 1 || candidates->globalCursorIndex() != 17) {
        reportFailure("wrap Up at first candidate of second emoji-history page", "page=1, global cursor=17",
                      "page=" + std::to_string(candidates->currentPage()) + ", global cursor=" + std::to_string(candidates->globalCursorIndex()),
                      "emoji candidate navigation crossed pages instead of wrapping within the current page");
        return 1;
    }
    return 0;
}
