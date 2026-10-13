/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file composition.h
 * @brief Composition list helpers, ported 1:1 from bamboo-core/bamboo_utils.go.
 *
 * `Composition` is the C++ spelling of Go's `[]*Transformation`: every function returns a new list
 * where Go returned a slice, which keeps the functional style of the original and removes Go's
 * slice-aliasing subtleties. `extractLastSyllable` lives in engine_utils.cpp because it needs isValid().
 */

#ifndef _LOTUS_COMPOSITION_H_
#define _LOTUS_COMPOSITION_H_

#include "types.h"
#include "transformation.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace lotus {

    /** @brief A list of composition nodes, 1:1 with Go's []*Transformation. */
    using Composition = std::vector<TransformationPtr>;

    /** @brief A composition split into the three parts of a syllable. */
    struct CvcParts {
        Composition firstConsonant; ///< Onset.
        Composition vowel;          ///< Rime vowels.
        Composition lastConsonant;  ///< Coda.
    };

    /** @brief A composition split before its last word. */
    struct WordSplit {
        Composition previous; ///< Everything before the last word.
        Composition last;     ///< The last word itself.
    };

    /**
     * @brief Copies a sub range of a composition, replacing Go's slice expressions.
     * @param source Source composition.
     * @param begin First index to copy.
     * @param end One past the last index to copy.
     * @return Copied nodes.
     */
    Composition sliceComposition(const Composition& source, std::size_t begin, std::size_t end);

    /**
     * @brief Last appending node of a composition, like Go's findLastAppendingTrans.
     * @param composition Composition.
     * @return The node, or null when there is none.
     */
    TransformationPtr findLastAppendingTrans(const Composition& composition);

    /**
     * @brief Appending nodes of a composition, like Go's filterAppendingComposition.
     * @param composition Composition.
     * @return Appending nodes in order.
     */
    Composition filterAppendingComposition(const Composition& composition);

    /**
     * @brief Follows the target chain to its root, like Go's findRootTarget.
     * @param target Starting node.
     * @return Root node (null when target is null).
     */
    TransformationPtr findRootTarget(const TransformationPtr& target);

    /**
     * @brief Last tone node with a target, like Go's getLastToneTransformation.
     * @param composition Composition.
     * @return The node, or null when there is none.
     */
    TransformationPtr getLastToneTransformation(const Composition& composition);

    /**
     * @brief Reports whether no node applies the effect to trans, like Go's isFree.
     * @param composition Composition.
     * @param trans Target node.
     * @param effectType Effect to look for.
     * @return True when the effect is free.
     */
    bool isFree(const Composition& composition, const TransformationPtr& trans, EffectType effectType);

    /**
     * @brief Reports whether target is one of the nodes of list, like Go's isIn.
     * @param target Node to look for.
     * @param list Candidate nodes.
     * @return True when the node is present.
     */
    bool isIn(const TransformationPtr& target, const Composition& list);

    /**
     * @brief Turns a composition back into appending nodes, like Go's breakComposition.
     * @param composition Composition.
     * @return Appending nodes of the non virtual keys.
     */
    Composition breakComposition(const Composition& composition);

    /**
     * @brief Appending node produced by a key, like Go's generateAppendingTrans.
     * @param rules Applicable rules.
     * @param lowerKey Lowercase key.
     * @param isUpperCase Whether the key was uppercase.
     * @return The node.
     */
    TransformationPtr generateAppendingTrans(const std::vector<Rule>& rules, char32_t lowerKey, bool isUpperCase);

    /**
     * @brief Splits appending nodes into first consonant, vowel and last consonant, like Go's
     *        extractCvcAppendingTrans.
     * @param composition Composition.
     * @return The three parts of the syllable.
     */
    CvcParts extractCvcAppendingTrans(const Composition& composition);

    /**
     * @brief Splits a composition into first consonant, vowel and last consonant, like Go's
     *        extractCvcTrans (effects follow their target).
     * @param composition Composition.
     * @return The three parts of the syllable.
     */
    CvcParts extractCvcTrans(const Composition& composition);

    /**
     * @brief Splits a composition before its last word, like Go's extractLastWord.
     * @param composition Composition.
     * @param effectKeys Keys that do not break a word.
     * @return Everything before the last word, then the last word.
     */
    WordSplit extractLastWord(const Composition& composition, std::u32string_view effectKeys);

    /**
     * @brief Splits a composition before its last word, spaces included, like Go's
     *        extractLastWordWithPunctuationMarks.
     * @param composition Composition.
     * @return Everything before the last word, then the last word.
     */
    WordSplit extractLastWordWithPunctuationMarks(const Composition& composition);

} // namespace lotus

#endif // _LOTUS_COMPOSITION_H_
