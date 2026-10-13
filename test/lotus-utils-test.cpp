// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ports bamboo-core/utils_test.go 1:1 (same cases, same expectations) plus the case map helpers.
#include "case_map.h"
#include "utils.h"

#include <cstdio>

namespace {

    using lotus::Mark;
    using lotus::Tone;
    using lotus::case_map::isLetter;
    using lotus::case_map::isUpper;
    using lotus::case_map::toLower;
    using lotus::case_map::toUpper;
    using lotus::utils::addMarkToChar;
    using lotus::utils::addToneToChar;
    using lotus::utils::findToneFromChar;
    using lotus::utils::isVowel;

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    constexpr std::u32string_view kAllVowels = U"aàáảãạăằắẳẵặâầấẩẫậeèéẻẽẹêềếểễệiìíỉĩịoòóỏõọôồốổỗộơờớởỡợuùúủũụưừứửữựyỳýỷỹỵ";

} // namespace

int run_utils() {
    check("a is a vowel", isVowel(U'a'));
    check("á is a vowel", isVowel(U'á'));
    check("b is not a vowel", !isVowel(U'b'));
    for (char32_t vowel : kAllVowels) {
        check("vowel table entry", isVowel(vowel));
    }

    check("tone of e", findToneFromChar(U'e') == Tone::None);
    check("tone of è", findToneFromChar(U'è') == Tone::Grave);
    check("tone of é", findToneFromChar(U'é') == Tone::Acute);
    check("tone of ẽ", findToneFromChar(U'ẽ') == Tone::Tilde);
    check("tone of ẻ", findToneFromChar(U'ẻ') == Tone::Hook);
    check("tone of ạ", findToneFromChar(U'ạ') == Tone::Dot);

    check("a + dot", addToneToChar(U'a', Tone::Dot) == U'ạ');
    check("y + none", addToneToChar(U'y', Tone::None) == U'y');
    check("y, no mark", addMarkToChar(U'y', Mark::None) == U'y');
    check("ạ + breve", addMarkToChar(U'ạ', Mark::Breve) == U'ặ');

    check("toUpper ơ", toUpper(U'ơ') == U'Ơ');
    check("toLower Ư", toLower(U'Ư') == U'ư');
    check("toUpper đ", toUpper(U'đ') == U'Đ');
    check("isUpper Ô", isUpper(U'Ô'));
    check("isLetter đ", isLetter(U'đ'));
    check("isLetter é", isLetter(U'é'));
    check("isLetter ơ", isLetter(U'ơ'));
    check("not a letter: !", !isLetter(U'!'));
    check("not a letter: 0", !isLetter(U'0'));
    check("toLower", toLower(U"ĐƯỜNG TiẾNG") == U"đường tiếng");
    check("toUpper", toUpper(U"đường") == U"ĐƯỜNG");

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus utils: all checks passed\n");
    return 0;
}
