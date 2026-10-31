/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "data/spelling_data.h"

#include <array>
#include <span>
#include <string_view>

namespace lotus::data {
    namespace {
        constexpr auto               kFirstConsonantRow0 = std::to_array<std::u32string_view>({
            U"b",  U"c",   U"ch", U"d", U"đ",  U"g",  U"gh", U"gi", U"h", U"k",  U"kh", U"kr", U"l", U"m", U"n",
            U"ng", U"ngh", U"nh", U"p", U"ph", U"qu", U"r",  U"s",  U"t", U"th", U"tr", U"v",  U"x", U"z",
        });

        constexpr auto               kVowelRow0 = std::to_array<std::u32string_view>({U"ê", U"i", U"ua", U"uê", U"uy", U"y"});
        constexpr auto               kVowelRow1 = std::to_array<std::u32string_view>({U"a", U"iê", U"oa", U"uyê", U"yê"});
        constexpr auto               kVowelRow2 = std::to_array<std::u32string_view>({U"â", U"ă", U"e", U"o", U"oo", U"ô", U"ơ", U"oe", U"u", U"ư", U"uâ", U"uô", U"ươ"});
        constexpr auto               kVowelRow3 = std::to_array<std::u32string_view>({U"oă"});
        constexpr auto               kVowelRow4 = std::to_array<std::u32string_view>({U"uơ"});
        constexpr auto               kVowelRow5 = std::to_array<std::u32string_view>({
            U"ai", U"ao", U"au", U"âu", U"ay",  U"ây", U"eo", U"êu",  U"ia",  U"iêu", U"iu", U"oai", U"oao", U"oay", U"oeo",
            U"oi", U"ôi", U"ơi", U"ưa", U"uây", U"ui", U"ưi", U"uôi", U"ươi", U"ươu", U"ưu", U"uya", U"uyu", U"uêu", U"yêu",
        });
        constexpr auto               kVowelRow6 = std::to_array<std::u32string_view>({U"ă", U"u"});
        constexpr auto               kVowelRow7 = std::to_array<std::u32string_view>({U"i"});

        constexpr auto               kLastConsonantRow0 = std::to_array<std::u32string_view>({U"ch", U"nh"});
        constexpr auto               kLastConsonantRow1 = std::to_array<std::u32string_view>({U"c", U"ng"});
        constexpr auto               kLastConsonantRow2 = std::to_array<std::u32string_view>({U"m", U"n", U"p", U"t"});
        constexpr auto               kLastConsonantRow3 = std::to_array<std::u32string_view>({U"k"});
        constexpr auto               kLastConsonantRow4 = std::to_array<std::u32string_view>({U"c"});

        constexpr auto               kVcRow0 = std::to_array<int>({0, 2});
        constexpr auto               kVcRow1 = std::to_array<int>({0, 1, 2});
        constexpr auto               kVcRow2 = std::to_array<int>({1, 2});
        constexpr auto               kVcRow3 = std::to_array<int>({1, 2});
        constexpr std::array<int, 0> kVcRow4{};
        constexpr std::array<int, 0> kVcRow5{};
        constexpr auto               kVcRow6 = std::to_array<int>({3});
        constexpr auto               kVcRow7 = std::to_array<int>({4});

        constexpr auto               kFirstConsonantRows = std::to_array<std::span<const std::u32string_view>>({kFirstConsonantRow0});
        constexpr auto               kVowelRows =
            std::to_array<std::span<const std::u32string_view>>({kVowelRow0, kVowelRow1, kVowelRow2, kVowelRow3, kVowelRow4, kVowelRow5, kVowelRow6, kVowelRow7});
        constexpr auto kLastConsonantRows =
            std::to_array<std::span<const std::u32string_view>>({kLastConsonantRow0, kLastConsonantRow1, kLastConsonantRow2, kLastConsonantRow3, kLastConsonantRow4});
        constexpr auto kVcRows = std::to_array<std::span<const int>>({kVcRow0, kVcRow1, kVcRow2, kVcRow3, kVcRow4, kVcRow5, kVcRow6, kVcRow7});
    } // namespace

    constinit const std::span<const std::span<const std::u32string_view>> kFirstConsonantSeqs{kFirstConsonantRows};
    constinit const std::span<const std::span<const std::u32string_view>> kVowelSeqs{kVowelRows};
    constinit const std::span<const std::span<const std::u32string_view>> kLastConsonantSeqs{kLastConsonantRows};
    constinit const std::span<const std::span<const int>>                 kVcMatrix{kVcRows};

} // namespace lotus::data
