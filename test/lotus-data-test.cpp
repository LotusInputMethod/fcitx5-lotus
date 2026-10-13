// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sanity checks for the generated data tables: counts and spot values must match the Go sources
// they were generated from (bamboo/bamboo-core/{charset_def,utils,spelling,input_method_def}.go).
#include "data/data.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

using namespace lotus::data;

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    template <typename T>
    const T& pick(const std::vector<std::pair<char32_t, T>>& table, char32_t key) {
        static const T empty{};
        for (const auto& entry : table) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return empty;
    }

} // namespace

int run_data() {
    // Charsets: 16 tables, 2112 entries, values byte identical to Go (including the raw-string tables).
    std::size_t totalEntries = 0;
    for (const auto& table : charsetDefinitions()) {
        totalEntries += table.second.size();
    }
    check("charset table count", charsetDefinitions().size() == 16);
    check("charset entry count", totalEntries == 2112);
    check("TCVN3 'đ'", charsetDefinitions().at("TCVN3 (ABC)").at(U'đ') == "\xC2\xAE");
    check("C string hex 'đ'", charsetDefinitions().at("Unicode C string Hex").at(U'đ') == "\\x111");
    check("NCR hex 'ơ'", charsetDefinitions().at("NCR Hex").at(U'ơ') == "&#x1A1;");
    check("NCR decimal 'đ'", charsetDefinitions().at("NCR Decimal").at(U'đ') == "&#273;");

    // Vowels, punctuation and mark families.
    check("vowel count", vowels().size() == 72);
    check("vowel head", vowels().starts_with(U"aàáảãạ"));
    check("vowel index 6", vowels()[6] == U'ă');
    check("vowel tail", vowels().substr(66, 6) == U"yỳýỷỹỵ");
    check("punctuation count", punctuationMarks().size() == 33);
    check("punctuation backslash", std::find(punctuationMarks().begin(), punctuationMarks().end(), U'\\') != punctuationMarks().end());
    check("mark family count", markFamilies().size() == 12);
    check("mark family 'ơ'", pick(markFamilies(), U'ơ') == U"oô_ơ_");
    check("mark family 'd'", pick(markFamilies(), U'd') == U"d___đ");
    check("mark family 'ê'", pick(markFamilies(), U'ê') == U"eê___");

    // Spelling tables.
    check("first consonant rows", firstConsonantSeqs().size() == 1);
    check("first consonant 'đ'", firstConsonantSeqs()[0][4] == U"đ");
    check("vowel rows", vowelSeqs().size() == 8);
    check("vowel row 5 size", vowelSeqs()[5].size() == 30);
    check("vowel row 0", vowelSeqs()[0][0] == U"ê");
    check("last consonant rows", lastConsonantSeqs().size() == 5);
    check("last consonant row 4", lastConsonantSeqs()[4][0] == U"c");
    check("vc matrix rows", vcMatrix().size() == 8);
    check("vc matrix row 0", vcMatrix()[0] == std::vector<int>{0, 2});
    check("vc matrix row 5 empty", vcMatrix()[5].empty());
    check("vc matrix row 7", vcMatrix()[7] == std::vector<int>{4});

    // Input methods: 7 definitions.
    check("input method count", inputMethodDefinitions().size() == 7);
    check("telex w", inputMethodDefinitions().at("Telex").at("w") == "UOA_ƯƠĂ");
    check("telex d", inputMethodDefinitions().at("Telex").at("d") == "D_Đ");
    check("vni 9", inputMethodDefinitions().at("VNI").at("9") == "D_Đ");
    check("viqr apostrophe", inputMethodDefinitions().at("VIQR").at("'") == "DauSac");
    check("french layout é", inputMethodDefinitions().at("VNI Bàn phím tiếng Pháp").at("é") == "DauSac");

    // Case map.
    check("case pair count", lowerUpperPairs().size() == 108);
    check("case pair ơ", pick(lowerUpperPairs(), U'ơ') == U'Ơ');
    check("case pair đ", pick(lowerUpperPairs(), U'đ') == U'Đ');
    check("letters sorted", std::is_sorted(letters().begin(), letters().end()));
    check("letters size", letters().size() == 217);
    check("letters has đ", std::binary_search(letters().begin(), letters().end(), U'đ'));
    check("letters has é", std::binary_search(letters().begin(), letters().end(), U'é'));
    check("letters has Z", std::binary_search(letters().begin(), letters().end(), U'Z'));
    check("letters has no digit", !std::binary_search(letters().begin(), letters().end(), U'0'));
    check("letters has no bang", !std::binary_search(letters().begin(), letters().end(), U'!'));

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus data: all checks passed\n");
    return 0;
}
