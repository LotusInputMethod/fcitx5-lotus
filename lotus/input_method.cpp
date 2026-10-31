/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file input_method.cpp
 * @brief Implementation of the input method builder.
 */
#include "input_method.h"

#include "case_map.h"
#include "rules_parser.h"
#include "utf8.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lotus {

    InputMethod parseInputMethod(const std::unordered_map<std::string, std::string>& definition) {
        InputMethod method;

        // Go iterates a map here, so the order of the definition keys is random; only the
        // key -> rules association matters, and the keys/toneKeys/appendingKeys lists are used
        // for membership tests. Sort the keys to keep the result deterministic.
        std::vector<std::pair<std::string, std::string>> entries(definition.begin(), definition.end());
        std::ranges::sort(entries);

        for (const auto& entry : entries) {
            const std::u32string keys = utf8::decode(entry.first);
            if (keys.empty()) {
                continue;
            }
            const std::u32string line   = utf8::decode(entry.second);
            const auto           parsed = parseRules(keys.front(), line);
            method.rules.insert(method.rules.end(), parsed.begin(), parsed.end());
            if (case_map::toLower(line).find(U"uo") != std::u32string::npos) {
                method.superKeys.push_back(keys.front());
            }
            method.keys.push_back(keys.front());
        }

        for (const auto& rule : method.rules) {
            if (rule.effectType == EffectType::Appending) {
                method.appendingKeys.push_back(rule.key);
            }
            if (rule.effectType == EffectType::ToneTransformation) {
                method.toneKeys.push_back(rule.key);
            }
        }
        return method;
    }

    const InputMethod& cachedInputMethod(InputMethodId id) {
        static const std::array<InputMethod, kInputMethodCount> kCache = [] {
            std::array<InputMethod, kInputMethodCount> cache;
            const auto&                                definitions = data::kInputMethodDefinitions;
            for (std::size_t i = 0; i < kInputMethodCount; ++i) {
                cache.at(i) = parseInputMethod(definitions.at(i));
            }
            return cache;
        }();
        return kCache.at(static_cast<std::size_t>(id));
    }

} // namespace lotus
