/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file rule.h
 * @brief Rule and input method models, 1:1 with Go's Rule and InputMethod.
 */

#ifndef _LOTUS_RULE_H_
#define _LOTUS_RULE_H_

#include "types.h"

#include <string>
#include <variant>
#include <vector>

namespace lotus {

    /** @brief One transformation rule, 1:1 with Go's Rule. */
    struct Rule {
        char32_t                 key        = 0;                     // Input key that produced the rule
        std::variant<Mark, Tone> effect     = Mark::None;            // Mark or tone, depending on effectType
        EffectType               effectType = EffectType::Appending; // Kind of transformation
        char32_t                 effectOn   = 0;                     // Character the effect applies to
        char32_t                 result     = 0;                     // Character the rule produces
        std::vector<Rule>        appendedRules;                      // Extra appending rules of the same key

        /** @brief Stores a tone in effect, like Go's Rule.SetTone. */
        void setTone(Tone tone) {
            effect = tone;
        }

        /** @brief Stores a mark in effect, like Go's Rule.SetMark. */
        void setMark(Mark mark) {
            effect = mark;
        }

        /** @brief Reads effect as a mark, Mark::None when it holds a tone instead. */
        [[nodiscard]] constexpr Mark mark() const {
            const Mark* const value = std::get_if<Mark>(&effect);
            return value != nullptr ? *value : Mark::None;
        }

        /** @brief Reads effect as a tone, Tone::None when it holds a mark instead. */
        [[nodiscard]] constexpr Tone tone() const {
            const Tone* const value = std::get_if<Tone>(&effect);
            return value != nullptr ? *value : Tone::None;
        }
    };

    /** @brief One input method with its parsed rules, 1:1 with Go's InputMethod. */
    struct InputMethod {
        std::string           name;          // Definition name, empty for an unknown name
        std::vector<Rule>     rules;         // Rules of every definition line, sorted by key
        std::vector<char32_t> superKeys;     // Keys whose line mentions "uo"
        std::vector<char32_t> toneKeys;      // Keys that produce tone rules
        std::vector<char32_t> appendingKeys; // Keys that produce appending rules
        std::vector<char32_t> keys;          // Every key of the definition
    };

} // namespace lotus

#endif // _LOTUS_RULE_H_
