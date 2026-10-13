/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file flattener.cpp
 * @brief Implementation of the composition renderer.
 */
#include "flattener.h"

#include "case_map.h"
#include "utils.h"

#include <string>
#include <utility>
#include <vector>

namespace lotus {

    Canvas renderCanvas(const Composition& composition, ModeSet mode) {
        Canvas canvas;
        canvas.text = renderInto(composition, mode, &canvas.nodes);
        return canvas;
    }

    std::u32string renderInto(const Composition& composition, ModeSet mode, std::vector<std::size_t>* nodes) {
        Composition                                            effects;
        std::vector<std::pair<std::size_t, TransformationPtr>> appendingList;
        appendingList.reserve(composition.size());
        const bool isEnglish = mode.has(Mode::EnglishMode);
        for (std::size_t index = 0; index < composition.size(); ++index) {
            const auto& trans       = composition[index];
            const bool  isAppending = trans->rule.effectType == EffectType::Appending;
            if (isEnglish || isAppending) {
                if (trans->rule.key == 0) {
                    continue;
                }
                appendingList.emplace_back(index, trans);
            } else if (trans->target != nullptr) {
                effects.push_back(trans);
            }
        }

        std::u32string canvas;
        canvas.reserve(appendingList.size());
        for (const auto& entry : appendingList) {
            const auto& [nodeIndex, appendingTrans] = entry;
            char32_t char32                         = 0;
            if (isEnglish) {
                char32 = appendingTrans->rule.key;
            } else {
                char32 = appendingTrans->rule.effectOn;
                for (const auto& effect : effects) {
                    if (effect->target == appendingTrans) {
                        if (effect->rule.effectType == EffectType::MarkTransformation) {
                            if (effect->rule.mark() == Mark::Raw) {
                                char32 = appendingTrans->rule.key;
                            } else {
                                char32 = utils::addMarkToChar(char32, effect->rule.mark());
                            }
                        } else if (effect->rule.effectType == EffectType::ToneTransformation) {
                            char32 = utils::addToneToChar(char32, effect->rule.tone());
                        }
                    }
                }
            }
            if (mode.has(Mode::ToneLess)) {
                char32 = utils::addToneToChar(char32, Tone::None);
            }
            if (mode.has(Mode::MarkLess)) {
                char32 = utils::addMarkToChar(char32, Mark::None);
            }
            if (mode.has(Mode::LowerCase)) {
                char32 = case_map::toLower(char32);
            } else if (appendingTrans->isUpperCase) {
                char32 = case_map::toUpper(char32);
            }
            canvas.push_back(char32);
            if (nodes != nullptr) {
                nodes->push_back(nodeIndex);
            }
        }
        return canvas;
    }

    std::u32string flatten(const Composition& composition, ModeSet mode) {
        return renderInto(composition, mode, nullptr);
    }

} // namespace lotus
