// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ports bamboo-core/rules_parser_test.go 1:1 (same lines, same expectations, including the rule
// counts and the index based assertions) plus direct cases for the hand written DSL matchers.
// The Go test TestParseRulesWithIm is empty and therefore has nothing to port.
#include "input_method.h"
#include "rules_parser.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>

namespace {

    using lotus::EffectType;
    using lotus::Mark;
    using lotus::Rule;
    using lotus::Tone;
    using lotus::getAppendingRule;
    using lotus::matchAppendingLetters;
    using lotus::matchDsl;
    using lotus::parseRules;
    using lotus::parseTonelessRules;

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    void checkMarkRule(const char* name, const std::vector<Rule>& rules, std::size_t index, char32_t key, Mark mark, char32_t effectOn) {
        const bool ok = index < rules.size() && rules[index].key == key && rules[index].effectType == EffectType::MarkTransformation && rules[index].mark() == mark &&
            rules[index].effectOn == effectOn;
        if (!ok) {
            std::printf("FAIL %s (index %zu)\n", name, index);
            ++failures;
        }
    }

    void checkAppendRule(const char* name, const Rule& rule, char32_t key, char32_t effectOn) {
        const bool ok = rule.key == key && rule.effectType == EffectType::Appending && rule.effectOn == effectOn;
        if (!ok) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

} // namespace

int run_rules_parser() {
    // TestParseToneRules
    {
        const auto rules = parseRules(U'z', U"XoaDauThanh");
        check("tone rule count", rules.size() == 1U);
        check("tone rule type", !rules.empty() && rules[0].effectType == EffectType::ToneTransformation);
        check("tone rule none", !rules.empty() && rules[0].tone() == Tone::None);

        const auto tilde = parseRules(U'x', U"DauNga");
        check("tilde rule count", tilde.size() == 1U);
        check("tilde rule type", !tilde.empty() && tilde[0].effectType == EffectType::ToneTransformation);
        check("tilde rule tone", !tilde.empty() && tilde[0].tone() == Tone::Tilde);
    }

    // TestParseTonelessRules
    {
        const auto dash = parseTonelessRules(U'd', U"D_Đ");
        check("D_Đ rule count", dash.size() == 2U);
        checkMarkRule("D_Đ first rule", dash, 0U, U'd', Mark::Dash, U'd');

        const auto appending = parseTonelessRules(U'{', U"_Ư");
        check("_Ư rule count", appending.size() == 1U);
        check("_Ư appending rule", !appending.empty() && appending[0].effectType == EffectType::Appending && appending[0].effectOn == U'Ư');

        const auto word = parseTonelessRules(U'w', U"UOA_ƯƠĂ");
        check("UOA_ƯƠĂ rule count", word.size() == 33U);
        checkMarkRule("UOA_ƯƠĂ index 0", word, 0U, U'w', Mark::Horn, U'u');
        checkMarkRule("UOA_ƯƠĂ index 7", word, 7U, U'w', Mark::Horn, U'o');
        checkMarkRule("UOA_ƯƠĂ index 20", word, 20U, U'w', Mark::Breve, U'a');

        const auto withAppending = parseTonelessRules(U'w', U"UOA_ƯƠĂ__Ư");
        check("UOA_ƯƠĂ__Ư rule count", withAppending.size() == 34U);
        checkMarkRule("UOA_ƯƠĂ__Ư index 20", withAppending, 20U, U'w', Mark::Breve, U'a');
        if (withAppending.size() > 33U) {
            checkAppendRule("UOA_ƯƠĂ__Ư index 33", withAppending[33], U'w', U'ư');
        } else {
            check("UOA_ƯƠĂ__Ư index 33", false);
        }
    }

    // TestAppendRule
    {
        const auto lower = parseTonelessRules(U'[', U"__ươ");
        check("__ươ rule count", lower.size() == 1U);
        check("__ươ appended count", !lower.empty() && lower[0].appendedRules.size() == 1U);
        if (!lower.empty() && lower[0].appendedRules.size() == 1U) {
            checkAppendRule("__ươ appended rule", lower[0].appendedRules[0], U'[', U'ơ');
        }

        const auto upper = parseTonelessRules(U'{', U"__ƯƠ");
        check("__ƯƠ rule count", upper.size() == 1U);
        check("__ƯƠ appended count", !upper.empty() && upper[0].appendedRules.size() == 1U);
        if (!upper.empty() && upper[0].appendedRules.size() == 1U) {
            checkAppendRule("__ƯƠ appended rule", upper[0].appendedRules[0], U'{', U'Ơ');
        }
    }

    // Hand written DSL matchers (replacements for the two Go regular expressions).
    {
        const auto simple = matchDsl(U"a_â");
        check("matchDsl a_â", simple.has_value() && simple->effectiveOns == U"a" && simple->results == U"â" && simple->appended.empty());

        const auto upper = matchDsl(U"UOA_ƯƠĂ");
        check("matchDsl UOA_ƯƠĂ", upper.has_value() && upper->effectiveOns == U"UOA" && upper->results == U"ƯƠĂ" && upper->appended.empty());

        const auto withAppended = matchDsl(U"uoa_ươă__ư");
        check("matchDsl with appended", withAppended.has_value() && withAppended->results == U"ươă" && withAppended->appended == U"__ư");

        const auto aeO = matchDsl(U"AEO_ÂÊÔ");
        check("matchDsl AEO_ÂÊÔ", aeO.has_value() && aeO->effectiveOns == U"AEO" && aeO->results == U"ÂÊÔ");

        const auto d = matchDsl(U"D_Đ");
        check("matchDsl D_Đ", d.has_value() && d->effectiveOns == U"D" && d->results == U"Đ");

        check("matchDsl __ă", !matchDsl(U"__ă").has_value());
        check("matchDsl empty", !matchDsl(U"").has_value());
        check("matchDsl no underscore", !matchDsl(U"abc").has_value());

        const auto appended = matchAppendingLetters(U"__ă");
        check("matchAppendingLetters __ă", appended.has_value() && *appended == U"ă");
        const auto single = matchAppendingLetters(U"_ă");
        check("matchAppendingLetters _ă", single.has_value() && *single == U"ă");
        check("matchAppendingLetters abc", !matchAppendingLetters(U"abc").has_value());
    }

    // parseInputMethod: the definition of an input method is parsed from its generated table entry.
    {
        const lotus::InputMethod telex = lotus::parseInputMethod(lotus::data::kInputMethodDefinitions.at(static_cast<std::size_t>(lotus::InputMethodId::Telex)));
        check("telex keys", telex.keys.size() == 11U);
        check("telex rules", !telex.rules.empty());
    }

    // cachedInputMethod: the parsed definitions are shared between callers (the addon parses the
    // configured input method for every input context and on every configuration refresh), so the
    // result must equal parseInputMethod() and stay the same object.
    {
        const lotus::InputMethod& first  = lotus::cachedInputMethod(lotus::InputMethodId::Telex);
        const lotus::InputMethod& second = lotus::cachedInputMethod(lotus::InputMethodId::Telex);
        check("cachedInputMethod reuses one object", &first == &second);

        const lotus::InputMethod parsed = lotus::parseInputMethod(lotus::data::kInputMethodDefinitions.at(static_cast<std::size_t>(lotus::InputMethodId::Telex)));
        bool same = first.keys == parsed.keys && first.superKeys == parsed.superKeys && first.toneKeys == parsed.toneKeys && first.appendingKeys == parsed.appendingKeys &&
            first.rules.size() == parsed.rules.size();
        for (std::size_t i = 0; same && i < parsed.rules.size(); ++i) {
            same = first.rules[i].key == parsed.rules[i].key && first.rules[i].effectType == parsed.rules[i].effectType && first.rules[i].effectOn == parsed.rules[i].effectOn &&
                first.rules[i].result == parsed.rules[i].result && first.rules[i].appendedRules.size() == parsed.rules[i].appendedRules.size();
        }
        check("cachedInputMethod matches parseInputMethod", same);

        check("custom has no built-in definition", lotus::cachedInputMethod(lotus::InputMethodId::Custom).rules.empty());
    }

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus rules parser: all checks passed\n");
    return 0;
}
