/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "data/vowel_data.h"

#include <algorithm>

namespace lotus::data {

    constexpr std::u32string_view kVowels = U"aàáảãạăằắẳẵặâầấẩẫậeèéẻẽẹêềếểễệiìíỉĩịoòóỏõọôồốổỗộơờớởỡợuùúủũụưừứửữựyỳýỷỹỵ";

    namespace {
        constexpr auto kPunctuationData = std::to_array<char32_t>({
            U',', U';', U':', U'.', U'\"', U'\'', U'!', U'?', U' ', U'<', U'>', U'=', U'+', U'-', U'*', U'/', U'\\',
            U'_', U'~', U'`', U'@', U'#',  U'$',  U'%', U'^', U'&', U'(', U')', U'{', U'}', U'[', U']', U'|',
        });
    } // namespace

    constinit const std::span<const char32_t>                                        kPunctuationMarks{kPunctuationData};

    constexpr std::array<std::pair<char32_t, std::u32string_view>, kMarkFamilyCount> kMarkFamilies{
        {
            {U'a', U"aâă__"},
            {U'd', U"d___đ"},
            {U'e', U"eê___"},
            {U'o', U"oô_ơ_"},
            {U'u', U"u__ư_"},
            {U'â', U"aâă__"},
            {U'ê', U"eê___"},
            {U'ô', U"oô_ơ_"},
            {U'ă', U"aâă__"},
            {U'đ', U"d___đ"},
            {U'ơ', U"oô_ơ_"},
            {U'ư', U"u__ư_"},
        },
    };

    namespace {
        constexpr char32_t    kAsciiLimit = 128;

        consteval std::size_t countNonAsciiVowels() {
            std::size_t count = 0;
            for (char32_t codePoint : kVowels) {
                count += codePoint >= kAsciiLimit ? 1U : 0U;
            }
            return count;
        }

        consteval std::size_t countNonAsciiFamilies() {
            std::size_t count = 0;
            for (const auto& [baseChar, family] : kMarkFamilies) {
                count += baseChar >= kAsciiLimit ? 1U : 0U;
            }
            return count;
        }

        consteval std::array<std::int8_t, kAsciiLimit> makeAsciiVowelPositions() {
            std::array<std::int8_t, kAsciiLimit> table{};
            table.fill(-1);
            for (std::size_t i = 0; i < kVowels.size(); ++i) {
                if (kVowels[i] < kAsciiLimit) {
                    table.at(kVowels[i]) = static_cast<std::int8_t>(i);
                }
            }
            return table;
        }

        consteval std::array<std::pair<char32_t, std::int8_t>, kNonAsciiVowelCount> makeVowelIndex() {
            std::array<std::pair<char32_t, std::int8_t>, kNonAsciiVowelCount> table{};
            std::size_t                                                       slot = 0;
            for (std::size_t i = 0; i < kVowels.size(); ++i) {
                if (kVowels[i] >= kAsciiLimit) {
                    table.at(slot) = {kVowels[i], static_cast<std::int8_t>(i)};
                    ++slot;
                }
            }
            std::ranges::sort(table, {}, &std::pair<char32_t, std::int8_t>::first);
            return table;
        }

        consteval std::array<std::u32string_view, kAsciiLimit> makeAsciiMarkFamilies() {
            std::array<std::u32string_view, kAsciiLimit> table{};
            for (const auto& [baseChar, family] : kMarkFamilies) {
                if (baseChar < kAsciiLimit) {
                    table.at(baseChar) = family;
                }
            }
            return table;
        }

        consteval std::array<std::pair<char32_t, std::u32string_view>, kNonAsciiFamilyCount> makeMarkFamilyIndex() {
            std::array<std::pair<char32_t, std::u32string_view>, kNonAsciiFamilyCount> table{};
            std::size_t                                                                slot = 0;
            for (const auto& [baseChar, family] : kMarkFamilies) {
                if (baseChar >= kAsciiLimit) {
                    table.at(slot) = {baseChar, family};
                    ++slot;
                }
            }
            std::ranges::sort(table, {}, &std::pair<char32_t, std::u32string_view>::first);
            return table;
        }

        consteval std::array<bool, kAsciiLimit> makeAsciiPunctuation() {
            std::array<bool, kAsciiLimit> table{};
            for (char32_t mark : kPunctuationData) {
                if (mark < kAsciiLimit) {
                    table.at(mark) = true;
                }
            }
            return table;
        }

        constexpr bool allPunctuationMarksAreAscii() {
            return std::ranges::all_of(kPunctuationData, [](char32_t mark) { return mark < kAsciiLimit; });
        }

        static_assert(countNonAsciiVowels() == kNonAsciiVowelCount);
        static_assert(countNonAsciiFamilies() == kNonAsciiFamilyCount);
        static_assert(allPunctuationMarksAreAscii());
    } // namespace

    constexpr std::array<std::int8_t, 128>                                               kAsciiVowelPositions = makeAsciiVowelPositions();
    constexpr std::array<std::pair<char32_t, std::int8_t>, kNonAsciiVowelCount>          kVowelIndex          = makeVowelIndex();
    constexpr std::array<std::u32string_view, 128>                                       kAsciiMarkFamilies   = makeAsciiMarkFamilies();
    constexpr std::array<std::pair<char32_t, std::u32string_view>, kNonAsciiFamilyCount> kMarkFamilyIndex     = makeMarkFamilyIndex();
    constexpr std::array<bool, 128>                                                      kAsciiPunctuation    = makeAsciiPunctuation();

    static_assert(std::ranges::is_sorted(kVowelIndex, {}, &std::pair<char32_t, std::int8_t>::first));
    static_assert(std::ranges::is_sorted(kMarkFamilyIndex, {}, &std::pair<char32_t, std::u32string_view>::first));

} // namespace lotus::data
