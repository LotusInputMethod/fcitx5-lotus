/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file rules_parser.h
 * @brief Parses the input method definition DSL, ported 1:1 from bamboo-core/rules_parser.go.
 *
 * The Go implementation uses two regular expressions with the `\p{L}` class, which std::regex
 * does not support. They are replaced by the hand written matchers matchDsl()/matchAppendingLetters(),
 * whose behaviour (leftmost match, greedy quantifiers, `\p{L}` = every letter of the generated
 * letter table) is kept identical.
 */

#ifndef _LOTUS_RULES_PARSER_H_
#define _LOTUS_RULES_PARSER_H_

#include "rule.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lotus {

    /** @brief Groups captured by the DSL pattern `([a-zA-Z]+)_(\p{L}+)([_\p{L}]*)`. */
    struct DslMatch {
        std::u32string effectiveOns; ///< Group 1: one or more ASCII letters
        std::u32string results;      ///< Group 2: one or more letters
        std::u32string appended;     ///< Group 3: zero or more '_' or letters
    };

    /**
     * @brief Matches the DSL pattern `([a-zA-Z]+)_(\p{L}+)([_\p{L}]*)`, like Go's regDsl.
     * @param line Definition line in code points.
     * @return Captured groups, or std::nullopt when the line does not match.
     */
    std::optional<DslMatch> matchDsl(std::u32string_view line);

    /**
     * @brief Matches the appending pattern `(_?)_(\p{L}+)` and returns its second group, like Go's
     *        regDslAppending with FindStringSubmatch(value)[2].
     * @param value Definition line in code points.
     * @return The letters after the underscore, or std::nullopt when the line does not match.
     */
    std::optional<std::u32string> matchAppendingLetters(std::u32string_view value);

    /**
     * @brief Tone of a definition line, like Go's tones table lookup.
     * @param name Definition line in code points; the names are ASCII.
     * @return Tone, or std::nullopt when the line names no tone.
     */
    std::optional<Tone> toneFromName(std::u32string_view name);

    /**
     * @brief Parses one definition line into rules, like Go's ParseRules.
     * @param key Input key.
     * @param line Definition line in code points.
     * @return Parsed rules.
     */
    std::vector<Rule> parseRules(char32_t key, std::u32string_view line);

    /**
     * @brief Parses a mark/appending definition line, like Go's ParseTonelessRules.
     * @param key Input key.
     * @param line Definition line in code points.
     * @return Parsed rules.
     */
    std::vector<Rule> parseTonelessRules(char32_t key, std::u32string_view line);

    /**
     * @brief Expands one mark rule over the family of effectiveOn, like Go's ParseToneLessRule.
     * @param key Input key.
     * @param effectiveOn Character the mark turns into result.
     * @param result Marked character.
     * @param effect Mark value.
     * @return Parsed rules.
     */
    std::vector<Rule> parseToneLessRule(char32_t key, char32_t effectiveOn, char32_t result, Mark effect);

    /**
     * @brief Builds the appending rule of a definition line, like Go's getAppendingRule.
     * @param key Input key.
     * @param value Definition line in code points.
     * @return The rule, or std::nullopt when the line has no appending part.
     */
    std::optional<Rule> getAppendingRule(char32_t key, std::u32string_view value);

} // namespace lotus

#endif // _LOTUS_RULES_PARSER_H_
