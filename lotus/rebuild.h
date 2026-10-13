/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file rebuild.h
 * @brief Rebuilds a composition from a Vietnamese text, ported 1:1 from bamboo-core/rebuild.go.
 */

#ifndef _LOTUS_REBUILD_H_
#define _LOTUS_REBUILD_H_

#include "composition.h"

#include <string>
#include <string_view>
#include <vector>

namespace lotus {

    /**
     * @brief Splits a text into words, keeping the delimiters, like Go's splitTextIntoWords.
     * @param text Text to split.
     * @return Words in order; every word break symbol is a word of its own.
     */
    std::vector<std::u32string> splitTextIntoWords(std::u32string_view text);

    /**
     * @brief Converts one word into transformations, like Go's processWordToComposition.
     * @param word Word to convert (a single word break symbol, or a run of ordinary code points).
     * @param stdStyle Use the modern tone placement rules, Go's stdStyle.
     * @return The transformations of the word.
     */
    Composition processWordToComposition(std::u32string_view word, bool stdStyle);

    /**
     * @brief Creates a composition (list of transformations) from a Vietnamese text, bypassing all
     *        input method rules, like Go's RebuildCompositionFromText.
     * @param text Text to rebuild from.
     * @param stdStyle Use the modern tone placement rules, Go's stdStyle.
     * @return The transformations of the whole text.
     */
    Composition rebuildCompositionFromText(std::u32string_view text, bool stdStyle);

} // namespace lotus

#endif // _LOTUS_REBUILD_H_
