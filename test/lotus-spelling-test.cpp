// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Covers bamboo-core/spelling.go through the generated tables: lookup(), isValidVC() and isValidCVC()
// including the "first consonant only" and "empty split" branches. The syllable level cases of
// bamboo_test.go ("nghieng", "ngieng", "tieengs", ...) go through IsValid() and are ported with the
// engine tests in step 10.
#include "spelling.h"

#include "data/spelling_data.h"

#include <cstdio>
#include <string_view>
#include <vector>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    void checkContains(const char* name, const std::vector<int>& indexes, int expected) {
        for (int index : indexes) {
            if (index == expected) {
                return;
            }
        }
        std::printf("FAIL %s: missing row %d\n", name, expected);
        ++failures;
    }

    void checkCvc(const char* name, std::u32string_view firstConsonant, std::u32string_view vowel, std::u32string_view lastConsonant, bool expected) {
        const bool actual = lotus::isValidCVC(firstConsonant, vowel, lastConsonant, true);
        if (actual != expected) {
            std::printf("FAIL %s: got %s, expected %s\n", name, actual ? "true" : "false", expected ? "true" : "false");
            ++failures;
        }
    }

} // namespace

int run_spelling() {
    // lookup(): a row matches when its token is no longer than the input (or exactly as long, with
    // inputIsFull), comparing tone-less characters unless inputIsComplete is set. Every matching row
    // is reported, so two rows can be valid for the same input ("c" hits the "ch"/"c" rows).
    checkContains("lookup first consonant ng exact", lotus::lookup(lotus::data::kFirstConsonantSeqs, U"ng", true, true), 0);
    checkContains("lookup vowel a", lotus::lookup(lotus::data::kVowelSeqs, U"a", false, true), 1);
    checkContains("lookup vowel uyê exact", lotus::lookup(lotus::data::kVowelSeqs, U"uyê", true, true), 1);
    checkContains("lookup vowel oă exact", lotus::lookup(lotus::data::kVowelSeqs, U"oă", true, true), 3);
    checkContains("lookup vowel uơ exact", lotus::lookup(lotus::data::kVowelSeqs, U"uơ", true, true), 4);
    checkContains("lookup last consonant c prefix", lotus::lookup(lotus::data::kLastConsonantSeqs, U"c", false, true), 0);
    checkContains("lookup last consonant c exact", lotus::lookup(lotus::data::kLastConsonantSeqs, U"c", true, true), 1);
    checkContains("lookup last consonant ch exact", lotus::lookup(lotus::data::kLastConsonantSeqs, U"ch", true, true), 0);
    check("lookup no match", lotus::lookup(lotus::data::kFirstConsonantSeqs, U"zzz", true, true).empty());
    // Without inputIsComplete the token is also matched without its marks ("a" matches "â").
    checkContains("lookup tone less match", lotus::lookup(lotus::data::kVowelSeqs, U"a", true, false), 2);

    // isValidVC(): the allowed vowel/last consonant pairs of vcMatrix.
    check("vc allows acute coda", lotus::isValidVC({1}, {1}));
    check("vc allows first coda row", lotus::isValidVC({1}, {0}));
    check("vc rejects unknown coda", !lotus::isValidVC({1}, {4}));

    // isValidCVC(): every branch of Go's function. The engine calls it with inputIsFullComplete true
    // at the end of a word, which is what these cases exercise.
    checkCvc("first consonant only", U"ng", U"", U"", true);
    checkCvc("q alone is rejected while full", U"q", U"", U"", false);
    checkCvc("unknown first consonant with vowel", U"zzz", U"a", U"", false);
    checkCvc("vowel only", U"", U"a", U"", true);
    checkCvc("vowel only uyê", U"", U"uyê", U"", true);
    checkCvc("vowel only oă", U"", U"oă", U"", true);
    checkCvc("vowel only uơ", U"", U"uơ", U"", true);
    checkCvc("vowel and coda", U"", U"a", U"ch", true);
    checkCvc("first consonant, vowel and coda", U"x", U"a", U"c", true);
    checkCvc("two part first consonant and coda", U"ng", U"a", U"ch", true);
    checkCvc("unknown vowel", U"b", U"zzz", U"", false);
    checkCvc("unknown coda", U"", U"a", U"zzz", false);
    checkCvc("empty split is rejected", U"", U"", U"", false);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus spelling: all checks passed\n");
    return 0;
}
