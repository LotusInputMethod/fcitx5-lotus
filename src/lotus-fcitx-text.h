/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoàng Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file lotus-fcitx-text.h
 * @brief The single UTF-32 to UTF-8 boundary of the addon.
 *
 * Everything inside the addon keeps engine text as std::u32string; only fcitx itself speaks UTF-8
 * (Text, commitString, surroundingText, key symbol labels). Every conversion lives here.
 */

#ifndef _FCITX5_LOTUS_FCITX_TEXT_H_
#define _FCITX5_LOTUS_FCITX_TEXT_H_

#include <chrono>
#include <string>
#include <string_view>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>

namespace lotus::fcitx_text {

    /**
     * @brief Renders a UTF-32 text as an fcitx Text with the cursor at the end.
     * @param text UTF-32 text.
     * @return fcitx Text (empty when text is empty).
     */
    fcitx::Text toText(std::u32string_view text);

    /**
     * @brief Commits a UTF-32 text to an input context.
     *
     * The single place that commits engine text: it encodes the text to UTF-8, hands it to fcitx,
     * logs it in UTF-8 and records the commit time the replacement delay is measured from. Nothing
     * happens when the context is null or the text is empty.
     * @param ic Input context to commit to.
     * @param text UTF-32 text.
     * @param lastCommitTime Commit timestamp of the caller, updated on every commit.
     */
    void commit(fcitx::InputContext* ic, std::u32string_view text, std::chrono::steady_clock::time_point& lastCommitTime);

    /**
     * @brief Code point of a key symbol, as a one code point text.
     * @param sym Key symbol.
     * @return One code point text, empty when the symbol has no Unicode mapping.
     */
    std::u32string fromKeySym(fcitx::KeySym sym);

} // namespace lotus::fcitx_text

#endif // _FCITX5_LOTUS_FCITX_TEXT_H_
