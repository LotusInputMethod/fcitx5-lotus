/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file case_map.h
 * @brief Unicode case helpers for the code points the engine deals with.
 *
 * Go relies on the unicode package (unicode.ToLower/ToUpper/IsUpper/IsLower/IsLetter). The
 * engine only ever sees ASCII, Latin-1 and Vietnamese letters, so the port uses a table
 * generated from the same Go data instead of shipping the whole Unicode database.
 */

#ifndef _LOTUS_CASE_MAP_H_
#define _LOTUS_CASE_MAP_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lotus::case_map {

    /**
     * @brief Looks a code point up in a sorted (from, to) table.
     * @param table Pairs sorted by their first element.
     * @param key Code point to look up.
     * @return The mapped code point, or key itself when it is absent from the table.
     */
    char32_t lookupCase(const std::vector<std::pair<char32_t, char32_t>>& table, char32_t key);

    /**
     * @brief Lowercases a code point (identity when it has no lowercase mapping, like Go).
     * @param codePoint Input code point.
     * @return Lowercase code point.
     */
    char32_t toLower(char32_t codePoint);

    /**
     * @brief Uppercases a code point (identity when it has no uppercase mapping, like Go).
     * @param codePoint Input code point.
     * @return Uppercase code point.
     */
    char32_t toUpper(char32_t codePoint);

    /**
     * @brief Reports whether a code point is an uppercase letter, like Go's unicode.IsUpper.
     * @param codePoint Input code point.
     * @return True when the code point has a lowercase mapping.
     */
    bool isUpper(char32_t codePoint);

    /**
     * @brief Reports whether a code point is a lowercase letter, like Go's unicode.IsLower.
     * @param codePoint Input code point.
     * @return True when the code point has an uppercase mapping.
     */
    bool isLower(char32_t codePoint);

    /**
     * @brief Reports whether a code point is a letter, like Go's unicode.IsLetter (ASCII/Latin-1/Vietnamese).
     * @param codePoint Input code point.
     * @return True for every code point in the generated letter table.
     */
    bool isLetter(char32_t codePoint);

    /**
     * @brief Lowercases every code point of a text, like Go's strings.ToLower.
     * @param text Input code points.
     * @return Lowercased code points.
     */
    std::u32string toLower(std::u32string_view text);

    /**
     * @brief Uppercases every code point of a text, like Go's strings.ToUpper.
     * @param text Input code points.
     * @return Uppercased code points.
     */
    std::u32string toUpper(std::u32string_view text);

} // namespace lotus::case_map

#endif // _LOTUS_CASE_MAP_H_
