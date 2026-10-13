// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hand built compositions covering every branch of bamboo-core/flattener.go (mark, tone, raw mark,
// English mode, virtual keys, ToneLess/MarkLess/LowerCase) plus the composition split helpers.
#include "composition.h"
#include "flattener.h"

#include <cstdio>
#include <memory>
#include <string>
#include <variant>

namespace {

    using lotus::Composition;
    using lotus::EffectType;
    using lotus::Mark;
    using lotus::Mode;
    using lotus::Rule;
    using lotus::Tone;
    using lotus::Transformation;
    using lotus::TransformationPtr;

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    void checkCanvas(const char* name, const Composition& composition, lotus::ModeSet mode, std::u32string_view expected) {
        const std::u32string actual = lotus::flatten(composition, mode);
        if (actual != expected) {
            std::printf("FAIL %s: got %zu code points, expected %zu\n", name, actual.size(), expected.size());
            ++failures;
        }
    }

    TransformationPtr makeEffect(const TransformationPtr& target, EffectType type, std::variant<Mark, Tone> effect) {
        auto trans             = std::make_shared<Transformation>();
        trans->target          = target;
        trans->rule.effectType = type;
        trans->rule.effect     = effect;
        return trans;
    }

    TransformationPtr makeAppending(char32_t key, char32_t effectOn, bool isUpperCase) {
        auto trans           = std::make_shared<Transformation>();
        trans->isUpperCase   = isUpperCase;
        trans->rule.key      = key;
        trans->rule.effectOn = effectOn;
        trans->rule.result   = effectOn;
        return trans;
    }

    constexpr lotus::ModeSet kVietnamese = lotus::toMask(Mode::VietnameseMode);
    constexpr lotus::ModeSet kEnglish    = lotus::toMask(Mode::EnglishMode);

} // namespace

int run_flattener() {
    // A single appending node renders its effectOn.
    checkCanvas("single append", {lotus::makeAppendingTrans(U'a', false)}, kVietnamese, U"a");

    // Mark then tone, in insertion order, turn a into ấ.
    const auto a     = lotus::makeAppendingTrans(U'a', false);
    const auto hat   = makeEffect(a, EffectType::MarkTransformation, Mark::Hat);
    const auto acute = makeEffect(a, EffectType::ToneTransformation, Tone::Acute);
    checkCanvas("mark", {a, hat}, kVietnamese, U"â");
    checkCanvas("mark and tone", {a, hat, acute}, kVietnamese, U"ấ");

    // ToneLess / MarkLess strip the effects, LowerCase lowers the rendered code point.
    checkCanvas("tone less", {a, hat, acute}, kVietnamese | lotus::toMask(Mode::ToneLess), U"â");
    checkCanvas("tone less and mark less", {a, hat, acute}, kVietnamese | lotus::toMask(Mode::ToneLess) | lotus::toMask(Mode::MarkLess), U"a");
    checkCanvas("upper case", {makeAppending(U'w', U'a', true)}, kVietnamese, U"A");
    checkCanvas("lower case", {makeAppending(U'w', U'A', true)}, kVietnamese | lotus::toMask(Mode::LowerCase), U"a");

    // MarkRaw falls back to the key that produced the node.
    const auto horn = makeAppending(U'w', U'ư', false);
    checkCanvas("raw mark", {horn, makeEffect(horn, EffectType::MarkTransformation, Mark::Raw)}, kVietnamese, U"w");

    // English mode renders the key, and a virtual key (key == 0) is skipped in both modes.
    checkCanvas("english mode", {horn, makeEffect(horn, EffectType::MarkTransformation, Mark::Horn)}, kEnglish, U"w");
    checkCanvas("virtual key skipped", {makeAppending(0, U'x', false)}, kVietnamese, U"");
    checkCanvas("virtual key skipped in english", {makeAppending(0, U'x', false)}, kEnglish, U"");

    // sliceComposition clamps to the source range.
    const Composition three = {lotus::makeAppendingTrans(U'a', false), lotus::makeAppendingTrans(U'b', false), lotus::makeAppendingTrans(U'c', false)};
    check("slice middle", lotus::sliceComposition(three, 1U, 3U).size() == 2U);
    check("slice beyond end", lotus::sliceComposition(three, 4U, 6U).empty());
    check("slice reversed range", lotus::sliceComposition(three, 2U, 1U).empty());

    // extractLastWord splits before the last word: "a b" -> [a, ' '] + [b].
    const auto             space = lotus::makeAppendingTrans(U' ', false);
    const auto             b     = lotus::makeAppendingTrans(U'b', false);
    const lotus::WordSplit parts = lotus::extractLastWord({lotus::makeAppendingTrans(U'a', false), space, b}, U"");
    check("split previous size", parts.previous.size() == 2U);
    check("split last size", parts.last.size() == 1U);

    // extractLastWord returns the whole composition when the last word reaches the end.
    const lotus::WordSplit single = lotus::extractLastWord(three, U"");
    check("split single word previous", single.previous.empty());
    check("split single word last", single.last.size() == 3U);

    // A word break at the very end keeps the whole composition in previous (the i == n - 1 branch).
    const lotus::WordSplit trailing = lotus::extractLastWord({lotus::makeAppendingTrans(U'a', false), lotus::makeAppendingTrans(U'b', false), space}, U"");
    check("split trailing break previous", trailing.previous.size() == 3U);
    check("split trailing break last", trailing.last.empty());

    // The first break found while walking backwards wins: the last node here is a space.
    const lotus::WordSplit twoBreaks = lotus::extractLastWord({lotus::makeAppendingTrans(U'a', false), space, lotus::makeAppendingTrans(U'b', false), space}, U"");
    check("split two breaks previous", twoBreaks.previous.size() == 4U);
    check("split two breaks last", twoBreaks.last.empty());

    // An empty composition falls through to the "no break" return.
    const lotus::WordSplit emptySplit = lotus::extractLastWord({}, U"");
    check("split empty previous", emptySplit.previous.empty());
    check("split empty last", emptySplit.last.empty());

    // The punctuation variant splits on a space anywhere in the middle, like the plain one.
    const lotus::WordSplit marks = lotus::extractLastWordWithPunctuationMarks({lotus::makeAppendingTrans(U'a', false), space, lotus::makeAppendingTrans(U'b', false)});
    check("split marks previous size", marks.previous.size() == 2U);
    check("split marks last size", marks.last.size() == 1U);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus flattener: all checks passed\n");
    return 0;
}
