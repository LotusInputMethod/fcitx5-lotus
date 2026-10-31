/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file case_data.h
 * @brief Data tables ported from the Go engine. Generated, do not edit.
 */
#ifndef _LOTUS_GENERATED_CASE_DATA_H_
#define _LOTUS_GENERATED_CASE_DATA_H_

#include <span>
#include <utility>

namespace lotus::data {
    /** @brief (lower, upper) code point pairs used by the engine. */
    extern const std::span<const std::pair<char32_t, char32_t>> kLowerUpperPairs;
    /** @brief (upper, lower) code point pairs, sorted by upper, for the reverse lookup. */
    extern const std::span<const std::pair<char32_t, char32_t>> kUpperLowerPairs;
    /** @brief Every code point the engine treats as a letter (ASCII, Latin-1, Vietnamese). */
    extern const std::span<const char32_t> kLetters;
} // namespace lotus::data

#endif // _LOTUS_GENERATED_CASE_DATA_H_
