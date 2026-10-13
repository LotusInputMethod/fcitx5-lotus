/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file utf8.cpp
 * @brief Implementation of the UTF-8 <-> UTF-32 helpers.
 */
#include "utf8.h"

namespace lotus::utf8 {

    constexpr char32_t kContinuationMask = 0xC0U;
    constexpr char32_t kContinuationBits = 0x80U;
    constexpr char32_t kTrailMask        = 0x3FU;

    bool               isContinuation(char c) {
        return (toCodePoint(c) & kContinuationMask) == kContinuationBits;
    }

    char32_t decodeOne(std::string_view in, std::size_t& index) {
        const char32_t    b0        = toCodePoint(in[index]);
        const std::size_t available = in.size() - index;
        const auto        trail     = [&](std::size_t offset) { return toCodePoint(in[index + offset]) & kTrailMask; };

        if (b0 < 0x80U) {
            ++index;
            return b0;
        }
        if (b0 >= 0xC2U && b0 <= 0xDFU && available >= 2U && isContinuation(in[index + 1])) {
            const char32_t codePoint = ((b0 & 0x1FU) << 6U) | trail(1U);
            index += 2U;
            return codePoint;
        }
        if (b0 >= 0xE0U && b0 <= 0xEFU && available >= 3U && isContinuation(in[index + 1]) && isContinuation(in[index + 2])) {
            const char32_t codePoint = ((b0 & 0x0FU) << 12U) | (trail(1U) << 6U) | trail(2U);
            if (codePoint >= 0x800U && (codePoint < 0xD800U || codePoint > 0xDFFFU)) {
                index += 3U;
                return codePoint;
            }
        }
        if (b0 >= 0xF0U && b0 <= 0xF4U && available >= 4U && isContinuation(in[index + 1]) && isContinuation(in[index + 2]) && isContinuation(in[index + 3])) {
            const char32_t codePoint = ((b0 & 0x07U) << 18U) | (trail(1U) << 12U) | (trail(2U) << 6U) | trail(3U);
            if (codePoint >= 0x10000U && codePoint <= 0x10FFFFU) {
                index += 4U;
                return codePoint;
            }
        }
        // Invalid sequence: consume a single byte, like Go's range over a string.
        ++index;
        return kReplacement;
    }

    std::size_t length(std::string_view in) {
        std::size_t count = 0;
        for (char c : in) {
            if (!isContinuation(c)) {
                ++count;
            }
        }
        return count;
    }

    char32_t last(std::string_view in) {
        if (in.empty()) {
            return 0;
        }
        std::size_t start  = in.size();
        std::size_t walked = 0;
        while (start > 0 && walked < 3U && isContinuation(in[start - 1])) {
            --start;
            ++walked;
        }
        if (start == 0) {
            return kReplacement;
        }
        --start;
        std::size_t    index     = start;
        const char32_t codePoint = decodeOne(in, index);
        return index == in.size() ? codePoint : kReplacement;
    }

    std::u32string decode(std::string_view in) {
        std::u32string out;
        out.reserve(in.size());
        std::size_t index = 0;
        while (index < in.size()) {
            out.push_back(decodeOne(in, index));
        }
        return out;
    }

    void appendEncoded(std::string& out, char32_t codePoint) {
        if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            // Go: string(rune) replaces out of range and surrogate halves with U+FFFD.
            codePoint = kReplacement;
        }
        if (codePoint < 0x80U) {
            out.push_back(static_cast<char>(codePoint));
            return;
        }
        if (codePoint < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        } else if (codePoint < 0x10000U) {
            out.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & kTrailMask)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & kTrailMask)));
            out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & kTrailMask)));
        }
        out.push_back(static_cast<char>(0x80U | (codePoint & kTrailMask)));
    }

    std::string encode(std::u32string_view in) {
        std::string out;
        out.reserve(in.size());
        for (char32_t codePoint : in) {
            appendEncoded(out, codePoint);
        }
        return out;
    }

    std::u32string sanitize(std::u32string_view text) {
        std::u32string out(text);
        for (char32_t& codePoint : out) {
            if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
                codePoint = kReplacement;
            }
        }
        return out;
    }

} // namespace lotus::utf8
