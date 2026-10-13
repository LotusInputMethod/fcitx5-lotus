/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file utils.h
 * @brief Character tables and primitives, ported 1:1 from bamboo-core/utils.go.
 */

#ifndef _LOTUS_UTILS_H_
#define _LOTUS_UTILS_H_

#include "types.h"

#include <optional>
#include <string_view>

namespace lotus::utils {

    /**
     * @brief The Vietnamese vowel table, 1:1 with Go's Vowels (12 bases x 6 tones).
     * @return Vowel code points.
     */
    std::u32string_view vowels();

    /**
     * @brief Reports whether a code point is a vowel, like Go's IsVowel.
     * @param codePoint Input code point.
     * @return True when the code point appears in the vowel table.
     */
    bool isVowel(char32_t codePoint);

    /**
     * @brief Position of a code point inside the vowel table, like Go's FindVowelPosition.
     * @param codePoint Input code point.
     * @return Index in the vowel table, or -1 when absent.
     */
    int findVowelPosition(char32_t codePoint);

    /**
     * @brief Mark family row of a code point, like Go's marksMaps lookup.
     * @param codePoint Input code point.
     * @return Five slot row ('_' marks an empty slot), or std::nullopt when there is no family.
     */
    std::optional<std::u32string_view> findMarkFamily(char32_t codePoint);

    /**
     * @brief Mark family of a code point with the empty slots removed, like Go's getMarkFamily.
     *
     * Parse-time only: it allocates, and the key loop never calls it.
     * @param codePoint Input code point.
     * @return Code points of the family, empty when the code point has no family.
     */
    std::u32string getMarkFamily(char32_t codePoint);

    /**
     * @brief Position of a code point inside its mark family row (empty slots included).
     * @param codePoint Input code point.
     * @return Index in the family row, or -1 when absent.
     */
    int findMarkPosition(char32_t codePoint);

    /**
     * @brief Mark that turns a code point into another one, like Go's FindMarkFromChar.
     * @param codePoint Input code point.
     * @return The mark, or std::nullopt when the code point belongs to no mark family.
     */
    std::optional<Mark> findMarkFromChar(char32_t codePoint);

    /**
     * @brief Applies a mark to a tone-less code point, like Go's AddMarkToTonelessChar.
     * @param codePoint Tone-less code point.
     * @param mark Mark to apply.
     * @return Code point with the mark applied (unchanged when the slot is empty).
     */
    char32_t addMarkToTonelessChar(char32_t codePoint, Mark mark);

    /**
     * @brief Applies a mark while preserving the current tone, like Go's AddMarkToChar.
     * @param codePoint Input code point.
     * @param mark Mark to apply.
     * @return Code point with the mark applied.
     */
    char32_t addMarkToChar(char32_t codePoint, Mark mark);

    /**
     * @brief Applies a tone, like Go's AddToneToChar.
     * @param codePoint Input code point.
     * @param tone Tone to apply.
     * @return Code point with the tone applied (unchanged when the code point is not a vowel).
     */
    char32_t addToneToChar(char32_t codePoint, Tone tone);

    /**
     * @brief Tone of a code point, like Go's FindToneFromChar.
     * @param codePoint Input code point.
     * @return Tone, Tone::None when the code point is not a vowel.
     */
    Tone findToneFromChar(char32_t codePoint);

    /**
     * @brief Reports whether a code point is a punctuation mark, like Go's IsPunctuationMark.
     * @param codePoint Input code point.
     * @return True for every punctuation mark of the table.
     */
    bool isPunctuationMark(char32_t codePoint);

    /**
     * @brief Reports whether a code point breaks a word, like Go's IsWordBreakSymbol.
     * @param codePoint Input code point.
     * @return True for punctuation marks and ASCII digits.
     */
    bool isWordBreakSymbol(char32_t codePoint);

    /**
     * @brief Reports whether a code point is an ASCII letter, like Go's IsAlpha.
     * @param codePoint Input code point.
     * @return True for A-Z and a-z.
     */
    bool isAlpha(char32_t codePoint);

    /**
     * @brief Reports whether a code point is a space, like Go's IsSpace.
     * @param codePoint Input code point.
     * @return True for U+0020.
     */
    bool isSpace(char32_t codePoint);

    /**
     * @brief Reports whether a code point is in a key list, like Go's inKeyList.
     * @param keys Key list.
     * @param key Input code point.
     * @return True when key appears in keys.
     */
    bool inKeyList(std::u32string_view keys, char32_t key);

    /**
     * @brief Reports whether the engine can process a key, like Go's canProcessKey.
     * @param lowerKey Lowercase key.
     * @param effectKeys Effect keys of the input method.
     * @return True when the key is a letter, an effect key, or a Vietnamese code point.
     */
    bool canProcessKey(char32_t lowerKey, std::u32string_view effectKeys);

    /**
     * @brief Reports whether a code point is a Vietnamese one, like Go's IsVietnameseRune.
     * @param lowerKey Lowercase key.
     * @return True when the key carries a tone or a mark.
     */
    bool isVietnameseRune(char32_t lowerKey);

    /**
     * @brief Reports whether a text contains a Vietnamese code point, like Go's HasAnyVietnameseRune.
     * @param word Input text.
     * @return True when at least one code point is Vietnamese.
     */
    bool hasAnyVietnameseRune(std::u32string_view word);

    /**
     * @brief Reports whether a text contains a vowel, like Go's HasAnyVietnameseVower.
     * @param word Input text.
     * @return True when at least one code point is a vowel.
     */
    bool hasAnyVietnameseVower(std::u32string_view word);

} // namespace lotus::utils

#endif // _LOTUS_UTILS_H_
