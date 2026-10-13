/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoàng Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file lotus-fcitx-text.cpp
 * @brief Implementation of the addon UTF-32 to UTF-8 boundary.
 */
#include "lotus-fcitx-text.h"

#include "lotus-utils.h"
#include "utf8.h"

#include <chrono>

namespace lotus::fcitx_text {

    fcitx::Text toText(std::u32string_view text) {
        fcitx::Text out;
        if (text.empty()) {
            return out;
        }
        // The engine only produces valid code points, so the encoded text needs no validation.
        out.append(utf8::encode(text), fcitx::TextFormatFlag::NoFlag);
        out.setCursor(static_cast<int>(out.textLength()));
        return out;
    }

    void commit(fcitx::InputContext* ic, std::u32string_view text, std::chrono::steady_clock::time_point& lastCommitTime) {
        if (ic == nullptr || text.empty()) {
            return;
        }
        const std::string utf8Text = utf8::encode(text);
        ic->commitString(utf8Text);
        LOTUS_INFO("Commit: " + utf8Text);
        lastCommitTime = std::chrono::steady_clock::now();
    }

    std::u32string fromKeySym(fcitx::KeySym sym) {
        const uint32_t codePoint = fcitx::Key::keySymToUnicode(sym);
        if (codePoint == 0U) {
            return {};
        }
        const std::u32string text(1, static_cast<char32_t>(codePoint));
        return text;
    }

} // namespace lotus::fcitx_text
