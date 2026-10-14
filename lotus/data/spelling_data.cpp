/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "data/spelling_data.h"

namespace lotus::data {

    const std::vector<std::vector<std::u32string_view>>& firstConsonantSeqs() {
        static const std::vector<std::vector<std::u32string_view>> kRows = {
            {
                U"b",  U"c",   U"ch", U"d", U"đ",  U"g",  U"gh", U"gi", U"h", U"k",  U"kh", U"kr", U"l", U"m", U"n",
                U"ng", U"ngh", U"nh", U"p", U"ph", U"qu", U"r",  U"s",  U"t", U"th", U"tr", U"v",  U"x", U"z",
            },
        };
        return kRows;
    }

    const std::vector<std::vector<std::u32string_view>>& vowelSeqs() {
        static const std::vector<std::vector<std::u32string_view>> kRows = {
            {
                U"ê",
                U"i",
                U"ua",
                U"uê",
                U"uy",
                U"y",
            },
            {
                U"a",
                U"iê",
                U"oa",
                U"uyê",
                U"yê",
            },
            {
                U"â",
                U"ă",
                U"e",
                U"o",
                U"oo",
                U"ô",
                U"ơ",
                U"oe",
                U"u",
                U"ư",
                U"uâ",
                U"uô",
                U"ươ",
            },
            {
                U"oă",
            },
            {
                U"uơ",
            },
            {
                U"ai", U"ao", U"au", U"âu", U"ay",  U"ây", U"eo", U"êu",  U"ia",  U"iêu", U"iu", U"oai", U"oao", U"oay", U"oeo",
                U"oi", U"ôi", U"ơi", U"ưa", U"uây", U"ui", U"ưi", U"uôi", U"ươi", U"ươu", U"ưu", U"uya", U"uyu", U"uêu", U"yêu",
            },
            {
                U"ă",
                U"u",
            },
            {
                U"i",
            },
        };
        return kRows;
    }

    const std::vector<std::vector<std::u32string_view>>& lastConsonantSeqs() {
        static const std::vector<std::vector<std::u32string_view>> kRows = {
            {
                U"ch",
                U"nh",
            },
            {
                U"c",
                U"ng",
            },
            {
                U"m",
                U"n",
                U"p",
                U"t",
            },
            {
                U"k",
            },
            {
                U"c",
            },
        };
        return kRows;
    }

    const std::vector<std::vector<int>>& vcMatrix() {
        static const std::vector<std::vector<int>> kMatrix = {
            {
                0,
                2,
            },
            {
                0,
                1,
                2,
            },
            {
                1,
                2,
            },
            {
                1,
                2,
            },
            {},
            {},
            {
                3,
            },
            {
                4,
            },
        };
        return kMatrix;
    }

} // namespace lotus::data
