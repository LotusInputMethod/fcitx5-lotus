/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file engine_utils.h
 * @brief Rule lookup, syllable validation and undo logic, ported 1:1 from
 *        bamboo-core/bamboo_utils.go.
 *
 * Go returns a nil slice from some of these functions (no transformation applicable, no undo needed)
 * and compares the result of getRightMostVowels with nil. An empty Composition carries the same
 * information for those call sites, so every nil slice of Go is returned as an empty Composition here.
 * The two regular expressions of the Go file (`regUOhTail`, `regUhO`) are replaced by matchesUOhTail
 * and matchesUhO; both are unanchored searches, like Go's regexp.MatchString.
 */

#ifndef _LOTUS_ENGINE_UTILS_H_
#define _LOTUS_ENGINE_UTILS_H_

#include "composition.h"
#include "rule.h"
#include "transformation.h"
#include "types.h"

#include <string_view>
#include <vector>

namespace lotus {

    /**
     * @brief Reports whether a composition is a valid Vietnamese syllable, like Go's isValid.
     * @param composition Composition.
     * @param inputIsFullComplete Whether the input is complete, Go's inputIsFullComplete.
     * @return True when the composition holds at most one node, or when its last tone and its
     *         first consonant / vowel / last consonant split are valid.
     */
    bool isValid(const Composition& composition, bool inputIsFullComplete);

    /**
     * @brief Vowels of the last syllable of a composition, like Go's getRightMostVowels.
     * @param composition Composition.
     * @return Vowel part of extractCvcTrans, empty when the composition has no vowel (Go returns nil).
     */
    Composition rightMostVowels(const Composition& composition);

    /**
     * @brief Vowel the last tone has to be applied to, like Go's findToneTarget.
     * @param composition Composition.
     * @param stdStyle Use the modern tone placement rules, Go's stdStyle.
     * @return The target node, or null when there is none (Go returns nil).
     */
    TransformationPtr findToneTarget(const Composition& composition, bool stdStyle);

    /**
     * @brief Reports whether a tone is allowed with the last consonant of a composition, like Go's
     *        hasValidTone.
     * @param composition Composition.
     * @param tone Tone that is about to be applied.
     * @return True when the tone is free of a last consonant constraint, or when the last consonant
     *         accepts it.
     */
    bool hasValidTone(const Composition& composition, Tone tone);

    /** @brief A rule that applies to a composition node. */
    struct TargetRule {
        TransformationPtr target; ///< Node the rule applies to, null when nothing applies.
        Rule              rule;   ///< Rule to apply.
    };

    /**
     * @brief Mark rule that can be applied to a composition, like Go's findMarkTarget.
     * @param composition Composition.
     * @param rules Rules to look through.
     * @return The target node and the rule producing a change; a null target and a default
     *         constructed Rule when no mark applies (Go returns nil, Rule{}).
     */
    TargetRule findMarkTarget(const Composition& composition, const std::vector<Rule>& rules);

    /**
     * @brief Rule to apply to a composition, tone rules first, like Go's findTarget.
     * @param composition Composition.
     * @param applicableRules Rules of the key that was typed.
     * @param flags Engine flags (EngineFlag::FreeToneMarking, EngineFlag::StdToneStyle, ...).
     * @return The target node and the rule to apply; a null target when nothing applies.
     */
    TargetRule findTarget(const Composition& composition, const std::vector<Rule>& applicableRules, EngineFlags flags);

    /**
     * @brief Transformations undoing the effects a key can no longer apply, like Go's
     *        generateUndoTransformations.
     * @param composition Composition.
     * @param rules Rules of the key that was typed.
     * @param flags Engine flags, Go's flags.
     * @return Undo transformations, empty when the key has nothing to undo (Go returns nil).
     */
    Composition generateUndoTransformations(const Composition& composition, const std::vector<Rule>& rules, EngineFlags flags);

    /**
     * @brief Transformations a key produces on a composition, like Go's generateTransformations.
     * @param composition Composition.
     * @param applicableRules Rules of the key that was typed.
     * @param flags Engine flags, Go's flags.
     * @param lowerKey Lowercase key that was typed.
     * @param isUpperCase Whether the key was typed uppercase.
     * @return The transformations, empty when the key cannot be applied (Go returns nil, which makes
     *         the caller fall back to generateFallbackTransformations).
     */
    Composition generateTransformations(const Composition& composition, const std::vector<Rule>& applicableRules, EngineFlags flags, char32_t lowerKey, bool isUpperCase);

    /**
     * @brief Transformations that append a key when no rule applies, like Go's
     *        generateFallbackTransformations.
     * @param applicableRules Rules of the key that was typed.
     * @param lowerKey Lowercase key that was typed.
     * @param isUpperCase Whether the key was typed uppercase.
     * @return The appending node followed by one node per appended rule of its rule.
     */
    Composition generateFallbackTransformations(const std::vector<Rule>& applicableRules, char32_t lowerKey, bool isUpperCase);

    /**
     * @brief Transformations moving the last tone to the new tone target, like Go's
     *        refreshLastToneTarget.
     * @param composition Composition.
     * @param stdStyle Use the modern tone placement rules, Go's stdStyle.
     * @return Clear and re-apply transformations, empty when the tone target did not change
     *         (Go returns nil).
     */
    Composition refreshLastToneTarget(const Composition& composition, bool stdStyle);

    /**
     * @brief Splits a composition into everything before its last syllable and the syllable itself,
     *        like Go's extractLastSyllable.
     * @param composition Composition.
     * @return Everything before the last syllable, then the longest suffix of the last word that is
     *         a valid syllable; the head of the last word that is not part of that suffix is moved
     *         to the first part (Go appends it to a nil slice there).
     */
    WordSplit extractLastSyllable(const Composition& composition);

    /**
     * @brief Reports whether a text contains the pattern of Go's regUOhTail, `(uơ|ưo)\p{L}+`.
     * @param text Text to search.
     * @return True when "uơ" or "ưo" is followed by at least one letter. Like Go's
     *         regexp.MatchString this is an unanchored search. The letters are the ones
     *         case_map::isLetter knows about (ASCII, Latin-1 and Vietnamese), which is the same
     *         approximation the DSL parser uses for `\p{L}`.
     */
    bool matchesUOhTail(std::u32string_view text);

    /**
     * @brief Reports whether a text contains the pattern of Go's regUhO, `(ưo|ươ)`.
     * @param text Text to search.
     * @return True when the text contains "ưo" or "ươ", like Go's regexp.MatchString.
     */
    bool matchesUhO(std::u32string_view text);

} // namespace lotus

#endif // _LOTUS_ENGINE_UTILS_H_
