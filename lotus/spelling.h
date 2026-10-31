/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file spelling.h
 * @brief Vietnamese orthography validation, ported 1:1 from bamboo-core/spelling.go.
 */

#ifndef _LOTUS_SPELLING_H_
#define _LOTUS_SPELLING_H_

#include <span>
#include <string_view>
#include <vector>

namespace lotus {

    /**
     * @brief Rows of a spelling table whose token matches the input, like Go's lookup.
     * @param sequences Spelling rows.
     * @param input Tone-less text to match.
     * @param inputIsFull Require the token to be no longer than the input.
     * @param inputIsComplete Compare the token as-is, without stripping its marks.
     * @return Matching row indexes; empty when nothing matches (Go returns a nil slice there).
     */
    std::vector<int> lookup(std::span<const std::span<const std::u32string_view>> sequences, std::u32string_view input, bool inputIsFull, bool inputIsComplete);

    /**
     * @brief Reports whether a vowel row and a last consonant row are allowed together, like Go's
     *        isValidVC.
     * @param vowelIndexes Matching vowel rows.
     * @param lastConsonantIndexes Matching last consonant rows.
     * @return True when the pair table allows at least one combination.
     */
    bool isValidVC(const std::vector<int>& vowelIndexes, const std::vector<int>& lastConsonantIndexes);

    /**
     * @brief Validates a first consonant / vowel / last consonant split, like Go's isValidCVC.
     * @param firstConsonant Tone-less first consonant.
     * @param vowel Tone-less vowel.
     * @param lastConsonant Tone-less last consonant.
     * @param inputIsFullComplete Whether the input is complete.
     * @return True when the split is a valid Vietnamese syllable.
     */
    bool isValidCVC(std::u32string_view firstConsonant, std::u32string_view vowel, std::u32string_view lastConsonant, bool inputIsFullComplete);

} // namespace lotus

#endif // _LOTUS_SPELLING_H_
