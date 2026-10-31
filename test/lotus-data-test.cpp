// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sanity checks for the generated data tables: counts and spot values must match the Go sources
// they were generated from (bamboo/bamboo-core/{charset_def,utils,spelling,input_method_def}.go).
#include "data/data.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

using namespace lotus::data;

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    template <typename Table>
    const Table::value_type::second_type& pick(const Table& table, char32_t key) {
        using Value = Table::value_type::second_type;
        static const Value empty{};
        for (const auto& entry : table) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return empty;
    }

} // namespace

int run_data() {
    // Charsets: the table is indexed by Charset, 16 entries for the 17 charsets (Unicode is empty),
    // 2112 entries total, values byte identical to Go.
    std::size_t totalEntries = 0;
    for (const auto& table : kCharsetTables) {
        totalEntries += table.size();
    }
    check("charset table count", kCharsetTables.size() == kCharsetCount);
    check("charset entry count", totalEntries == 2112);
    check("Unicode has no table", kCharsetTables.at(static_cast<std::size_t>(Charset::Unicode)).empty());
    check("TCVN3 'đ'", kCharsetTables.at(static_cast<std::size_t>(Charset::Tcvn3Abc)).at(U'đ') == "\xC2\xAE");
    check("C string hex 'đ'", kCharsetTables.at(static_cast<std::size_t>(Charset::UnicodeCStringHex)).at(U'đ') == "\\x111");
    check("NCR hex 'ơ'", kCharsetTables.at(static_cast<std::size_t>(Charset::NcrHex)).at(U'ơ') == "&#x1A1;");
    check("NCR decimal 'đ'", kCharsetTables.at(static_cast<std::size_t>(Charset::NcrDecimal)).at(U'đ') == "&#273;");

    // Vowels, punctuation and mark families.
    check("vowel count", kVowels.size() == 72);
    check("vowel head", kVowels.starts_with(U"aàáảãạ"));
    check("vowel index 6", kVowels[6] == U'ă');
    check("vowel tail", kVowels.substr(66, 6) == U"yỳýỷỹỵ");
    check("punctuation count", kPunctuationMarks.size() == 33);
    check("punctuation backslash", std::ranges::find(kPunctuationMarks, U'\\') != kPunctuationMarks.end());
    check("mark family count", kMarkFamilies.size() == 12);
    check("mark family 'ơ'", pick(kMarkFamilies, U'ơ') == U"oô_ơ_");
    check("mark family 'd'", pick(kMarkFamilies, U'd') == U"d___đ");
    check("mark family 'ê'", pick(kMarkFamilies, U'ê') == U"eê___");

    // Spelling tables.
    check("first consonant rows", kFirstConsonantSeqs.size() == 1);
    check("first consonant 'đ'", kFirstConsonantSeqs[0][4] == U"đ");
    check("vowel rows", kVowelSeqs.size() == 8);
    check("vowel row 5 size", kVowelSeqs[5].size() == 30);
    check("vowel row 0", kVowelSeqs[0][0] == U"ê");
    check("last consonant rows", kLastConsonantSeqs.size() == 5);
    check("last consonant row 4", kLastConsonantSeqs[4][0] == U"c");
    check("vc matrix rows", kVcMatrix.size() == 8);
    check("vc matrix row 0", std::ranges::equal(kVcMatrix[0], std::array<int, 2>{0, 2}));
    check("vc matrix row 5 empty", kVcMatrix[5].empty());
    check("vc matrix row 7", std::ranges::equal(kVcMatrix[7], std::array<int, 1>{4}));

    // Input methods: 8 ids, the 7 built-in ones have a definition and Custom has none.
    check("input method count", kInputMethodDefinitions.size() == kInputMethodCount);
    check("custom has no definition", kInputMethodDefinitions.at(static_cast<std::size_t>(InputMethodId::Custom)).empty());
    std::size_t definedMethods = 0;
    for (const auto& definition : kInputMethodDefinitions) {
        if (!definition.empty()) {
            ++definedMethods;
        }
    }
    check("built-in definition count", definedMethods == 7);
    check("telex w", kInputMethodDefinitions.at(static_cast<std::size_t>(InputMethodId::Telex)).at("w") == "UOA_ƯƠĂ");
    check("telex d", kInputMethodDefinitions.at(static_cast<std::size_t>(InputMethodId::Telex)).at("d") == "D_Đ");
    check("vni 9", kInputMethodDefinitions.at(static_cast<std::size_t>(InputMethodId::Vni)).at("9") == "D_Đ");
    check("viqr apostrophe", kInputMethodDefinitions.at(static_cast<std::size_t>(InputMethodId::Viqr)).at("'") == "DauSac");
    check("french layout é", kInputMethodDefinitions.at(static_cast<std::size_t>(InputMethodId::VniFrench)).at("é") == "DauSac");

    // Case map.
    check("case pair count", kLowerUpperPairs.size() == 82);
    check("case pair ơ", pick(kLowerUpperPairs, U'ơ') == U'Ơ');
    check("case pair đ", pick(kLowerUpperPairs, U'đ') == U'Đ');
    check("letters sorted", std::ranges::is_sorted(kLetters));
    check("letters size", kLetters.size() == 165);
    check("letters has đ", std::ranges::binary_search(kLetters, U'đ'));
    check("letters has é", std::ranges::binary_search(kLetters, U'é'));
    check("letters has no ascii Z", !std::ranges::binary_search(kLetters, U'Z'));
    check("letters has no digit", !std::ranges::binary_search(kLetters, U'0'));
    check("letters has no bang", !std::ranges::binary_search(kLetters, U'!'));

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus data: all checks passed\n");
    return 0;
}
