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
#include "data/input_method_data.h"
#include "rules_parser.h"
#include "utf8.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lotus {

    InputMethod parseInputMethod(const std::unordered_map<std::string, std::string>& definition, std::string_view name) {
        InputMethod method;
        method.name = name;

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

    InputMethod parseInputMethod(std::string_view name) {
        const auto& definitions = data::inputMethodDefinitions();
        const auto  definition  = definitions.find(std::string(name));
        if (definition == definitions.end()) {
            return InputMethod{};
        }
        return parseInputMethod(definition->second, name);
    }

    const InputMethod& cachedInputMethod(std::string_view name) {
        static const std::unordered_map<std::string, InputMethod> kCache = [] {
            std::unordered_map<std::string, InputMethod> cache;
            for (const auto& entry : data::inputMethodDefinitions()) {
                cache.emplace(entry.first, parseInputMethod(entry.first));
            }
            return cache;
        }();
        static const InputMethod kUnknown;
        const auto               found = kCache.find(std::string(name));
        return found != kCache.end() ? found->second : kUnknown;
    }

    const std::vector<std::string>& inputMethodNames() {
        // Go's order table (bamboo-c.go:279-287): the definitions live in a map, so the menu order
        // has to be spelled out instead of derived from the data.
        static const std::vector<std::string> kNames = {
            "Telex", "VNI", "Telex + VNI", "Telex + VNI + VIQR", "VIQR", "Microsoft layout", "VNI Bàn phím tiếng Pháp",
        };
        return kNames;
    }

} // namespace lotus
