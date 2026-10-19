/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file rebuild.cpp
 * @brief Implementation of the text to composition rebuild helpers and Engine::rebuildFromText.
 */
#include "rebuild.h"

#include "case_map.h"
#include "engine.h"
#include "engine_utils.h"
#include "utils.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lotus {

    Composition rebuildCompositionFromText(std::u32string_view text, bool stdStyle) {
        Composition                       composition;

        const std::vector<std::u32string> words = splitTextIntoWords(text);

        for (const std::u32string& word : words) {
            const Composition wordComposition = processWordToComposition(word, stdStyle);
            composition.insert(composition.end(), wordComposition.begin(), wordComposition.end());
        }

        return composition;
    }

    std::vector<std::u32string> splitTextIntoWords(std::u32string_view text) {
        std::vector<std::u32string> words;
        std::u32string              currentWord;

        for (const char32_t ch : text) {
            if (utils::isWordBreakSymbol(ch)) {
                if (!currentWord.empty()) {
                    words.push_back(currentWord);
                    currentWord.clear();
                }
                words.emplace_back(1, ch);
            } else {
                currentWord.push_back(ch);
            }
        }

        if (!currentWord.empty()) {
            words.push_back(std::move(currentWord));
        }

        return words;
    }

    Composition processWordToComposition(std::u32string_view word, bool stdStyle) {
        Composition composition;

        // Go tests len(word), the UTF-8 length; every word break symbol is ASCII, so the code point
        // count of the single symbol word is the same.
        if (word.size() == 1U && utils::isWordBreakSymbol(word[0])) {
            const char32_t ch            = word[0];
            const bool     isUpperCase   = case_map::isUpper(ch);
            auto           appendTrans   = std::make_shared<Transformation>();
            appendTrans->isUpperCase     = isUpperCase;
            appendTrans->rule.key        = ch;
            appendTrans->rule.effectOn   = ch;
            appendTrans->rule.effectType = EffectType::Appending;
            appendTrans->rule.result     = ch;
            return Composition{appendTrans};
        }

        for (const char32_t ch : word) {
            const char32_t            lowerCh     = case_map::toLower(ch);
            const bool                isUpperCase = case_map::isUpper(ch);

            const Tone                tone    = utils::findToneFromChar(lowerCh);
            const std::optional<Mark> mark    = utils::findMarkFromChar(lowerCh);
            const bool                hasMark = mark.has_value() && *mark != Mark::None;

            char32_t                  rootChar = lowerCh;
            if (tone != Tone::None) {
                rootChar = utils::addToneToChar(rootChar, Tone::None);
            }
            if (hasMark) {
                rootChar = utils::addMarkToChar(rootChar, Mark::None);
            }

            auto appendTrans             = std::make_shared<Transformation>();
            appendTrans->isUpperCase     = isUpperCase;
            appendTrans->rule.key        = rootChar;
            appendTrans->rule.effectOn   = rootChar;
            appendTrans->rule.effectType = EffectType::Appending;
            appendTrans->rule.result     = rootChar;
            composition.push_back(appendTrans);

            if (hasMark) {
                auto markTrans             = std::make_shared<Transformation>();
                markTrans->target          = appendTrans;
                markTrans->rule.key        = 0;
                markTrans->rule.effectType = EffectType::MarkTransformation;
                markTrans->rule.setMark(*mark);
                markTrans->rule.effectOn = rootChar;
                markTrans->rule.result   = utils::addMarkToTonelessChar(rootChar, *mark);
                composition.push_back(markTrans);
            }
        }

        Tone lastTone = Tone::None;
        for (const char32_t ch : word) {
            const Tone tone = utils::findToneFromChar(case_map::toLower(ch));
            if (tone != Tone::None) {
                lastTone = tone;
            }
        }

        if (lastTone != Tone::None) {
            const TransformationPtr toneTarget = findToneTarget(composition, stdStyle);
            if (toneTarget != nullptr) {
                auto toneTrans             = std::make_shared<Transformation>();
                toneTrans->target          = toneTarget;
                toneTrans->rule.key        = 0;
                toneTrans->rule.effectType = EffectType::ToneTransformation;
                toneTrans->rule.setTone(lastTone);
                composition.push_back(toneTrans);
            }
        }

        return composition;
    }

    void Engine::rebuildFromText(std::u32string_view text) {
        reset();
        composition_ = rebuildCompositionFromText(text, flags_.has(EngineFlag::StdToneStyle));
    }

} // namespace lotus
