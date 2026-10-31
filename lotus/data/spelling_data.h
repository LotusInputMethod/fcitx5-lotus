/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file spelling_data.h
 * @brief Data tables ported from the Go engine. Generated, do not edit.
 */
#ifndef _LOTUS_GENERATED_SPELLING_DATA_H_
#define _LOTUS_GENERATED_SPELLING_DATA_H_

#include <span>
#include <string_view>

namespace lotus::data {
    /** @brief First consonant sequences (spelling rows). */
    extern const std::span<const std::span<const std::u32string_view>> kFirstConsonantSeqs;
    /** @brief Vowel sequences (row i pairs with vcMatrix()[i]). */
    extern const std::span<const std::span<const std::u32string_view>> kVowelSeqs;
    /** @brief Last consonant sequences (spelling rows). */
    extern const std::span<const std::span<const std::u32string_view>> kLastConsonantSeqs;
    /** @brief Vowel row -> allowed last consonant rows. */
    extern const std::span<const std::span<const int>> kVcMatrix;
} // namespace lotus::data

#endif // _LOTUS_GENERATED_SPELLING_DATA_H_
