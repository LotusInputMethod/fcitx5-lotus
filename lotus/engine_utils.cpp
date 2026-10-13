/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file engine_utils.cpp
 * @brief Implementation of the rule lookup, syllable validation and undo helpers.
 */
#include "engine_utils.h"

#include "case_map.h"
#include "flattener.h"
#include "spelling.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lotus {

    bool isValid(const Composition& composition, bool inputIsFullComplete) {
        if (composition.size() <= 1U) {
            return true;
        }
        // Last tone checking.
        for (auto it = composition.rbegin(); it != composition.rend(); ++it) {
            if ((*it)->rule.effectType == EffectType::ToneTransformation) {
                if (!hasValidTone(composition, (*it)->rule.tone())) {
                    return false;
                }
                break;
            }
        }
        // Spell checking.
        const CvcParts cvc         = extractCvcTrans(composition);
        const ModeSet  flattenMode = toMask(Mode::VietnameseMode) | toMask(Mode::LowerCase) | toMask(Mode::ToneLess);
        return isValidCVC(flatten(cvc.firstConsonant, flattenMode), flatten(cvc.vowel, flattenMode), flatten(cvc.lastConsonant, flattenMode), inputIsFullComplete);
    }

    Composition rightMostVowels(const Composition& composition) {
        return extractCvcTrans(composition).vowel;
    }

    TransformationPtr findToneTarget(const Composition& composition, bool stdStyle) {
        if (composition.empty()) {
            return nullptr;
        }
        TransformationPtr target;
        const CvcParts    cvc           = extractCvcTrans(composition);
        const auto&       vowel         = cvc.vowel;
        const auto&       lastConsonant = cvc.lastConsonant;
        const Composition vowels        = filterAppendingComposition(vowel);
        if (vowels.size() == 1U) {
            target = vowels[0];
        } else if (vowels.size() == 2U && stdStyle) {
            for (const auto& trans : vowel) {
                if (trans->rule.result == U'ơ' || trans->rule.result == U'ê') {
                    target = trans->target != nullptr ? trans->target : trans;
                }
            }
            if (target == nullptr) {
                target = lastConsonant.empty() ? vowels[0] : vowels[1];
            }
        } else if (vowels.size() == 2U) {
            if (!lastConsonant.empty()) {
                target = vowels[1];
            } else {
                const std::u32string flattened = flatten(vowels, toMask(Mode::EnglishMode) | toMask(Mode::LowerCase) | toMask(Mode::ToneLess) | toMask(Mode::MarkLess));
                if (flattened == U"oa" || flattened == U"oe" || flattened == U"uy" || flattened == U"ue" || flattened == U"uo") {
                    target = vowels[1];
                } else {
                    target = vowels[0];
                }
            }
        } else if (vowels.size() == 3U) {
            if (flatten(vowels, toMask(Mode::EnglishMode) | toMask(Mode::LowerCase) | toMask(Mode::ToneLess) | toMask(Mode::MarkLess)) == U"uye") {
                target = vowels[2];
            } else {
                target = vowels[1];
            }
        }
        return target;
    }

    bool hasValidTone(const Composition& composition, Tone tone) {
        if (tone == Tone::None || tone == Tone::Acute || tone == Tone::Dot) {
            return true;
        }
        const CvcParts cvc           = extractCvcTrans(composition);
        const auto&    lastConsonant = cvc.lastConsonant;
        if (lastConsonant.empty()) {
            return true;
        }
        const std::u32string lastConsonants = flatten(lastConsonant, toMask(Mode::EnglishMode) | toMask(Mode::LowerCase));
        // These last consonants only go with the acute and dot tones.
        constexpr std::array<std::u32string_view, 5> kDotWithConsonants = {U"c", U"k", U"p", U"t", U"ch"};
        return std::ranges::none_of(kDotWithConsonants, [&lastConsonants](std::u32string_view sequence) { return lastConsonants == sequence; });
    }

    TargetRule findMarkTarget(const Composition& composition, const std::vector<Rule>& rules) {
        const std::u32string str = flatten(composition, toMask(Mode::VietnameseMode));
        for (auto it = composition.rbegin(); it != composition.rend(); ++it) {
            const TransformationPtr& trans = *it;
            for (const auto& rule : rules) {
                if (rule.effectType != EffectType::MarkTransformation) {
                    continue;
                }
                if (trans->rule.result != rule.effectOn || rule.mark() == Mark::None) {
                    continue;
                }
                const TransformationPtr target = findRootTarget(trans);
                auto                    effect = std::make_shared<Transformation>();
                effect->target                 = target;
                effect->rule                   = rule;
                Composition withEffect         = composition;
                withEffect.push_back(effect);
                if (str == flatten(withEffect, toMask(Mode::VietnameseMode))) {
                    continue;
                }
                if (isValid(withEffect, false)) {
                    return TargetRule{.target = target, .rule = rule};
                }
            }
        }
        return TargetRule{};
    }

    TargetRule findTarget(const Composition& composition, const std::vector<Rule>& applicableRules, EngineFlags flags) {
        const std::u32string str = flatten(composition, toMask(Mode::VietnameseMode));
        // Find the tone target first.
        for (const auto& applicableRule : applicableRules) {
            if (applicableRule.effectType != EffectType::ToneTransformation) {
                continue;
            }
            TransformationPtr target;
            if (flags.has(EngineFlag::FreeToneMarking)) {
                if (hasValidTone(composition, applicableRule.tone())) {
                    target = findToneTarget(composition, flags.has(EngineFlag::StdToneStyle));
                }
            } else {
                const TransformationPtr lastAppending = findLastAppendingTrans(composition);
                if (lastAppending != nullptr && utils::isVowel(lastAppending->rule.effectOn)) {
                    target = lastAppending;
                }
            }
            if (target == nullptr) {
                continue;
            }
            auto candidate         = std::make_shared<Transformation>();
            candidate->target      = target;
            candidate->rule        = applicableRule;
            Composition withEffect = composition;
            withEffect.push_back(candidate);
            if (str == flatten(withEffect, toMask(Mode::VietnameseMode))) {
                continue;
            }
            if (applicableRule.tone() == Tone::None && isFree(composition, target, EffectType::ToneTransformation) && utils::findToneFromChar(target->rule.result) == Tone::None) {
                target = nullptr;
            }
            return {.target = target, .rule = applicableRule};
        }
        return findMarkTarget(composition, applicableRules);
    }

    Composition generateUndoTransformations(const Composition& composition, const std::vector<Rule>& rules, EngineFlags flags) {
        Composition          transformations;
        const ModeSet        flattenMode = toMask(Mode::VietnameseMode) | toMask(Mode::ToneLess) | toMask(Mode::LowerCase);
        const std::u32string str         = flatten(composition, flattenMode);
        transformations.reserve(rules.size());
        for (const auto& rule : rules) {
            if (rule.effectType == EffectType::ToneTransformation) {
                TransformationPtr target;
                if (flags.has(EngineFlag::FreeToneMarking)) {
                    if (hasValidTone(composition, rule.tone())) {
                        target = findToneTarget(composition, flags.has(EngineFlag::StdToneStyle));
                    }
                } else {
                    const TransformationPtr lastAppending = findLastAppendingTrans(composition);
                    if (lastAppending != nullptr && utils::isVowel(lastAppending->rule.effectOn)) {
                        target = lastAppending;
                    }
                }
                if (target == nullptr) {
                    continue;
                }
                auto undo             = std::make_shared<Transformation>();
                undo->target          = target;
                undo->rule.effectType = EffectType::ToneTransformation;
                undo->rule.setTone(Tone::None);
                undo->rule.key = 0;
                transformations.push_back(undo);
            } else if (rule.effectType == EffectType::MarkTransformation) {
                for (auto it = composition.rbegin(); it != composition.rend(); ++it) {
                    const TransformationPtr& trans = *it;
                    if (trans->rule.result != rule.effectOn) {
                        continue;
                    }
                    auto undo             = std::make_shared<Transformation>();
                    undo->target          = findRootTarget(trans);
                    undo->rule.key        = 0;
                    undo->rule.effectType = EffectType::MarkTransformation;
                    undo->rule.setMark(Mark::None);
                    Composition withEffect = composition;
                    withEffect.push_back(undo);
                    if (str == flatten(withEffect, flattenMode)) {
                        continue;
                    }
                    transformations.push_back(undo);
                }
            }
        }
        return transformations;
    }

    Composition generateTransformations(const Composition& composition, const std::vector<Rule>& applicableRules, EngineFlags flags, char32_t lowerKey, bool isUpperCase) {
        Composition transformations;
        // Double typing an effect key undoes it and its effects, e.g. w + w -> w.
        if (!composition.empty()) {
            const Rule& lastRule = composition.back()->rule;
            if (lastRule.effectType == EffectType::Appending && lastRule.key == lowerKey && lastRule.key != lastRule.result) {
                auto raw             = std::make_shared<Transformation>();
                raw->rule.effectType = EffectType::MarkTransformation;
                raw->rule.setMark(Mark::Raw);
                raw->rule.key = 0;
                raw->target   = composition.back();
                transformations.push_back(raw);
                return transformations;
            }
        }
        // A target may be applied by many different transformations, e.g. o + o + w -> ơ.
        const TargetRule found = findTarget(composition, applicableRules, flags);
        if (found.target != nullptr) {
            auto trans         = std::make_shared<Transformation>();
            trans->rule        = found.rule;
            trans->target      = found.target;
            trans->isUpperCase = isUpperCase;
            transformations.push_back(trans);
            if (found.rule.effectType != EffectType::MarkTransformation) {
                return transformations;
            }
            Composition newComposition = composition;
            newComposition.insert(newComposition.end(), transformations.begin(), transformations.end());
            if (isValid(newComposition, true)) {
                return transformations;
            }
            // Implement the uow typing shortcut by creating a virtual Mark_HORN rule that targets
            // 'u' or 'o'.
            const TargetRule virtualFound = findTarget(newComposition, applicableRules, flags);
            if (virtualFound.target != nullptr) {
                Rule rule                 = virtualFound.rule;
                rule.key                  = 0;
                auto virtualTrans         = std::make_shared<Transformation>();
                virtualTrans->rule        = std::move(rule);
                virtualTrans->target      = virtualFound.target;
                virtualTrans->isUpperCase = false;
                transformations.push_back(virtualTrans);
            }
        } else {
            // Implement ươ/ưo(i/c/ng) + o -> uô.
            if (matchesUhO(flatten(composition, toMask(Mode::VietnameseMode) | toMask(Mode::ToneLess) | toMask(Mode::LowerCase)))) {
                const Composition vowels = filterAppendingComposition(rightMostVowels(composition));
                if (!vowels.empty()) {
                    auto clearEffect             = std::make_shared<Transformation>();
                    clearEffect->target          = vowels[0];
                    clearEffect->rule.effectType = EffectType::MarkTransformation;
                    clearEffect->rule.key        = 0;
                    clearEffect->rule.setMark(Mark::None);
                    Composition withEffect = composition;
                    withEffect.push_back(clearEffect);
                    const TargetRule newFound = findTarget(withEffect, applicableRules, flags);
                    if (newFound.target != nullptr && !isSame(newFound.target, vowels[0])) {
                        transformations.push_back(clearEffect);
                        auto trans         = std::make_shared<Transformation>();
                        trans->rule        = newFound.rule;
                        trans->target      = newFound.target;
                        trans->isUpperCase = isUpperCase;
                        transformations.push_back(trans);
                        return transformations;
                    }
                }
            }
            const Composition undoTransformations = generateUndoTransformations(composition, applicableRules, flags);
            if (!undoTransformations.empty()) {
                // If an effect key cannot find its target, it tries to undo its effects, e.g.
                // ươ + w -> uow.
                transformations.insert(transformations.end(), undoTransformations.begin(), undoTransformations.end());
                transformations.push_back(makeAppendingTrans(lowerKey, isUpperCase));
            }
        }
        return transformations;
    }

    Composition generateFallbackTransformations(const std::vector<Rule>& applicableRules, char32_t lowerKey, bool isUpperCase) {
        Composition             transformations;
        const TransformationPtr trans = generateAppendingTrans(applicableRules, lowerKey, isUpperCase);
        transformations.reserve(trans->rule.appendedRules.size() + 1U);
        transformations.push_back(trans);
        for (const auto& appendedRule : trans->rule.appendedRules) {
            Rule       rule                = appendedRule;
            const bool isAppendedUpperCase = isUpperCase || case_map::isUpper(rule.effectOn);
            rule.key                       = 0;
            rule.effectOn                  = case_map::toLower(rule.effectOn);
            rule.result                    = rule.effectOn;
            auto appendedTrans             = std::make_shared<Transformation>();
            appendedTrans->rule            = rule;
            appendedTrans->isUpperCase     = isAppendedUpperCase;
            transformations.push_back(appendedTrans);
        }
        return transformations;
    }

    Composition refreshLastToneTarget(const Composition& composition, bool stdStyle) {
        Composition transformations;
        // Go compares the vowels with nil, which only happens when the composition holds no vowel at
        // all, so an empty result carries the same information here.
        const Composition       rightmostVowels = rightMostVowels(composition);
        const TransformationPtr lastToneTrans   = getLastToneTransformation(composition);
        if (rightmostVowels.empty() || lastToneTrans == nullptr) {
            return transformations;
        }
        const TransformationPtr newToneTarget = findToneTarget(composition, stdStyle);
        if (!isSame(lastToneTrans->target, newToneTarget)) {
            auto clearTone             = std::make_shared<Transformation>();
            clearTone->target          = lastToneTrans->target;
            clearTone->rule.key        = 0;
            clearTone->rule.effectType = EffectType::ToneTransformation;
            clearTone->rule.setTone(Tone::None);
            transformations.push_back(clearTone);
            Rule overrideRule     = lastToneTrans->rule;
            overrideRule.key      = 0;
            auto overrideTrans    = std::make_shared<Transformation>();
            overrideTrans->target = newToneTarget;
            overrideTrans->rule   = std::move(overrideRule);
            transformations.push_back(overrideTrans);
        }
        return transformations;
    }

    WordSplit extractLastSyllable(const Composition& composition) {
        const WordSplit split    = extractLastWord(composition, U"");
        Composition     previous = split.previous;
        Composition     last     = split.last;
        std::size_t     anchor   = 0U;
        for (std::size_t i = 0U; i < last.size(); ++i) {
            if (!isValid(sliceComposition(last, anchor, i + 1U), false)) {
                anchor = i;
            }
        }
        if (anchor > 0U) {
            const Composition head = sliceComposition(last, 0U, anchor);
            previous.insert(previous.end(), head.begin(), head.end());
        }
        return WordSplit{.previous = previous, .last = sliceComposition(last, anchor, last.size())};
    }

    bool matchesUOhTail(std::u32string_view text) {
        constexpr std::array<std::u32string_view, 2> kHeads = {U"uơ", U"ưo"};
        return std::ranges::any_of(kHeads, [text](std::u32string_view head) {
            for (std::size_t position = text.find(head); position != std::u32string_view::npos; position = text.find(head, position + 1U)) {
                const std::size_t tail = position + head.size();
                if (tail < text.size() && case_map::isLetter(text[tail])) {
                    return true;
                }
            }
            return false;
        });
    }

    bool matchesUhO(std::u32string_view text) {
        return text.find(U"ưo") != std::u32string_view::npos || text.find(U"ươ") != std::u32string_view::npos;
    }

} // namespace lotus
