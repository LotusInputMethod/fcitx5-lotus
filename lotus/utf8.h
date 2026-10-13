/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file utf8.h
 * @brief UTF-8 <-> UTF-32 helpers for the Lotus engine.
 *
 * The engine keeps its state in UTF-32 (`char32_t`), so every conversion happens at the
 * boundary: keys arrive as UCS-4 and commit/preedit leave as UTF-8. The decoding rules match
 * Go's `for range string` / `utf8.DecodeRuneInString`: an invalid sequence yields U+FFFD and
 * consumes a single byte.
 */

#ifndef _LOTUS_UTF8_H_
#define _LOTUS_UTF8_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace lotus::utf8 {

    /** @brief Replacement character returned for every invalid UTF-8 sequence. Go: utf8.RuneError. */
    inline constexpr char32_t kReplacement = 0xFFFD;

    /**
     * @brief Reads one byte as a code point value.
     * @param c Input byte.
     * @return The byte value, widened without sign extension.
     */
    constexpr char32_t toCodePoint(char c) {
        return static_cast<char32_t>(static_cast<unsigned char>(c));
    }

    /**
     * @brief Reports whether a byte is a UTF-8 continuation byte.
     * @param c Input byte.
     * @return True for a 0b10xxxxxx byte.
     */
    bool isContinuation(char c);

    /**
     * @brief Decodes one UTF-8 sequence at in[index] and advances index past the consumed bytes.
     * @param in UTF-8 input.
     * @param index Byte offset of the sequence; advanced by the number of bytes consumed.
     * @return Decoded code point, kReplacement for an invalid sequence.
     */
    char32_t decodeOne(std::string_view in, std::size_t& index);

    /**
     * @brief Counts the code points of a UTF-8 string without validating it.
     * @param in UTF-8 string.
     * @return Number of code points (invalid bytes count as one code point each).
     */
    std::size_t length(std::string_view in);

    /**
     * @brief Returns the last code point of a UTF-8 string.
     * @param in UTF-8 string.
     * @return Last code point, 0 for an empty string, kReplacement for an invalid tail.
     */
    char32_t last(std::string_view in);

    /**
     * @brief Decodes UTF-8 into UTF-32.
     * @param in UTF-8 string.
     * @return Code points; every invalid sequence becomes kReplacement and consumes one byte.
     */
    std::u32string decode(std::string_view in);

    /**
     * @brief Encodes a code point into UTF-8 and appends it to out.
     * @param out Destination string.
     * @param codePoint Code point; out of range or surrogate halves become kReplacement.
     */
    void appendEncoded(std::string& out, char32_t codePoint);

    /**
     * @brief Encodes UTF-32 into UTF-8.
     * @param in Code points.
     * @return UTF-8 string.
     */
    std::string encode(std::u32string_view in);

    /**
     * @brief Replaces the code points a UTF-8 round trip cannot carry with kReplacement.
     *
     * The result is the exact effect of `decode(encode(text))`: every surrogate half and every code
     * point above U+10FFFF becomes kReplacement, like Go's string(rune), and everything else is kept.
     * @param text Code points.
     * @return Sanitized code points.
     */
    std::u32string sanitize(std::u32string_view text);

} // namespace lotus::utf8

#endif // _LOTUS_UTF8_H_
