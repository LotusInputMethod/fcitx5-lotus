/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file case_map.cpp
 * @brief Implementation of the Unicode case helpers.
 */
#include "case_map.h"

#include "data/case_data.h"

#include <algorithm>
#include <utility>

namespace lotus::case_map {
    constexpr char32_t kAsciiLimit = 128;

    char32_t           lookupCase(std::span<const std::pair<char32_t, char32_t>> table, char32_t key) {
        const auto it = std::ranges::lower_bound(table, key, std::less{}, [](const std::pair<char32_t, char32_t>& entry) { return entry.first; });
        return (it != table.end() && it->first == key) ? it->second : key;
    }

    char32_t toLower(char32_t codePoint) {
        if (codePoint < kAsciiLimit) {
            if (U'A' <= codePoint && codePoint <= U'Z') {
                return codePoint + (U'a' - U'A');
            }
            return codePoint;
        }
        return lookupCase(data::kUpperLowerPairs, codePoint);
    }

    char32_t toUpper(char32_t codePoint) {
        if (codePoint < kAsciiLimit) {
            if (U'a' <= codePoint && codePoint <= U'z') {
                return codePoint - (U'a' - U'A');
            }
            return codePoint;
        }
        return lookupCase(data::kLowerUpperPairs, codePoint);
    }

    bool isUpper(char32_t codePoint) {
        return toLower(codePoint) != codePoint;
    }

    bool isLower(char32_t codePoint) {
        return toUpper(codePoint) != codePoint;
    }

    bool isLetter(char32_t codePoint) {
        if (codePoint < kAsciiLimit) {
            return (U'a' <= codePoint && codePoint <= U'z') || (U'A' <= codePoint && codePoint <= U'Z');
        }
        return std::ranges::binary_search(data::kLetters, codePoint);
    }

    std::u32string toLower(std::u32string_view text) {
        std::u32string out(text);
        for (char32_t& codePoint : out) {
            codePoint = toLower(codePoint);
        }
        return out;
    }

    std::u32string toUpper(std::u32string_view text) {
        std::u32string out(text);
        for (char32_t& codePoint : out) {
            codePoint = toUpper(codePoint);
        }
        return out;
    }

} // namespace lotus::case_map
