// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"
#include "test-util.h"

#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

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
    const auto    historyPath = std::filesystem::path(getEnv("XDG_CONFIG_HOME")) / "fcitx5/conf/lotus-emoji-history.conf";
    std::ofstream history(historyPath);
    // History is capped at the newest 9 (EmojiLoader::kMaxEmojiHistory), so a
    // single page is the most this mode can show from history alone.
    const std::array<const char*, 9> emoji = {"😀", "😁", "😂", "😃", "😄", "😅", "😆", "😉", "😊"};
    for (size_t i = 0; i < emoji.size(); ++i)
        history << "history" << i << '=' << emoji.at(i) << '\n';
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
    if (!candidates || candidates->totalSize() != 9 || candidates->pageSize() != 9) {
        reportFailure("load emoji history fixture", "9 candidates with page size 9",
                      candidates ? "candidates=" + std::to_string(candidates->totalSize()) + ", page-size=" + std::to_string(candidates->pageSize()) : "no candidate list",
                      "EmojiLoader::loadHistory did not provide the capped single-page fixture to the real emoji mode");
        return 1;
    }

    // Move Up from the first candidate. This must wrap to the last candidate
    // of the same page, rather than changing page.
    if (!send(engine, entry, *context, FcitxKey_Up))
        return 1;

    if (candidates->currentPage() != 0 || candidates->globalCursorIndex() != 8) {
        reportFailure("wrap Up at first candidate of emoji-history page", "page=0, global cursor=8",
                      "page=" + std::to_string(candidates->currentPage()) + ", global cursor=" + std::to_string(candidates->globalCursorIndex()),
                      "emoji candidate navigation crossed pages instead of wrapping within the current page");
        return 1;
    }

    // Move Down from the last candidate. This must wrap to the first
    // candidate of the same page.
    if (!send(engine, entry, *context, FcitxKey_Down))
        return 1;

    if (candidates->currentPage() != 0 || candidates->globalCursorIndex() != 0) {
        reportFailure("wrap Down at last candidate of emoji-history page", "page=0, global cursor=0",
                      "page=" + std::to_string(candidates->currentPage()) + ", global cursor=" + std::to_string(candidates->globalCursorIndex()),
                      "emoji candidate navigation crossed pages instead of wrapping within the current page");
        return 1;
    }
    return 0;
}
