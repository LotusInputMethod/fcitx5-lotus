/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file vowel_data.h
 * @brief Data tables ported from the Go engine. Generated, do not edit.
 */
#ifndef _LOTUS_GENERATED_VOWEL_DATA_H_
#define _LOTUS_GENERATED_VOWEL_DATA_H_

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace lotus::data {
    /** @brief Number of mark family rows, for the tables derived from it. */
    inline constexpr std::size_t kMarkFamilyCount = 12U;
    /** @brief Non-ASCII vowels, i.e. the entries of the sorted vowel index. */
    inline constexpr std::size_t kNonAsciiVowelCount = 66U;
    /** @brief Non-ASCII mark families, i.e. the entries of the sorted family index. */
    inline constexpr std::size_t kNonAsciiFamilyCount = 7U;

    /** @brief The Vietnamese vowel table (12 bases x 6 tones), 1:1 with Go's Vowels. */
    extern const std::u32string_view kVowels;
    /** @brief Punctuation marks that break a word. */
    extern const std::span<const char32_t> kPunctuationMarks;
    /** @brief Mark families (rune -> family row), 1:1 with Go's marksMaps; '_' marks an absent slot. */
    extern const std::array<std::pair<char32_t, std::u32string_view>, kMarkFamilyCount> kMarkFamilies;

    /** @brief ASCII code point -> position in kVowels, -1 when the code point is not a vowel. */
    extern const std::array<std::int8_t, 128> kAsciiVowelPositions;
    /** @brief (code point, position in kVowels) of the non-ASCII vowels, sorted by code point. */
    extern const std::array<std::pair<char32_t, std::int8_t>, kNonAsciiVowelCount> kVowelIndex;
    /** @brief ASCII code point -> mark family row, empty when the code point has no family. */
    extern const std::array<std::u32string_view, 128> kAsciiMarkFamilies;
    /** @brief (code point, mark family row) of the non-ASCII marks, sorted by code point. */
    extern const std::array<std::pair<char32_t, std::u32string_view>, kNonAsciiFamilyCount> kMarkFamilyIndex;
    /** @brief ASCII code point -> punctuation mark flag. */
    extern const std::array<bool, 128> kAsciiPunctuation;
} // namespace lotus::data

#endif // _LOTUS_GENERATED_VOWEL_DATA_H_
