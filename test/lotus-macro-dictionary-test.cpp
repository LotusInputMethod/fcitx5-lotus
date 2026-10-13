// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ports bamboo/macrotable_test.go (TestMacroTableEmpty, TestMacroTableGet) and the single test of
// bamboo/dictionary_test.go (TestDictionaryLookup) 1:1 (same inputs, same expectations), plus two
// cases for loading a dictionary file. Go's nil *MacroTable / *dictionary are replaced by a
// default-constructed object: on the C++ side the engine holds both in a std::shared_ptr, so a
// missing table or dictionary is a null pointer the caller checks before calling them.
#include "dictionary.h"
#include "macro_table.h"
#include "utf8.h"

#include <cstdio>
#include <string>
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

    void checkU32(const char* name, std::u32string_view actual, std::u32string_view expected) {
        if (actual != expected) {
            std::printf("FAIL %s: got [%s] expected [%s]\n", name, lotus::utf8::encode(actual).c_str(), lotus::utf8::encode(expected).c_str());
            ++failures;
        }
    }

    // TestMacroTableEmpty.
    void case_macro_table_empty() {
        const lotus::MacroTable nilTable;
        check("nil MacroTable: Empty() got [false] expected [true]", nilTable.empty());

        const lotus::MacroTable emptyTable{};
        check("empty MacroTable: Empty() got [false] expected [true]", emptyTable.empty());

        lotus::MacroTable nonEmpty;
        nonEmpty.set("vn", "Việt Nam");
        check("non-empty MacroTable: Empty() got [true] expected [false]", !nonEmpty.empty());
    }

    // TestMacroTableGet.
    void case_macro_table_get() {
        lotus::MacroTable table;
        table.set("vn", "Việt Nam");

        const auto vnValue = table.get(U"VN");
        check("Get(VN) got [missing] expected [found] (case-insensitive)", vnValue.has_value());
        checkU32("Get(VN) got [Việt Nam] expected [Việt Nam] (case-insensitive)", vnValue.value_or(U""), U"Việt Nam");

        const auto vnLower = table.get(U"vn");
        check("Get(vn) got [missing] expected [found]", vnLower.has_value());
        checkU32("Get(vn) got [Việt Nam] expected [Việt Nam]", vnLower.value_or(U""), U"Việt Nam");

        check("Get(empty key) got [found] expected [missing]", !table.get(U"").has_value());

        check("Get(xyz) got [found] expected [missing]", !table.get(U"xyz").has_value());

        const lotus::MacroTable emptyTable;
        check("Get on empty table got [found] expected [missing]", !emptyTable.get(U"vn").has_value());
    }

    // TestDictionaryLookup.
    void case_dictionary_lookup() {
        lotus::Dictionary dictionary;
        dictionary.loadFromWords({"đcu", "chào", ""});

        check("hasWord missed a dictionary word: [chào đcu]", dictionary.hasWord(U"đcu") && dictionary.hasWord(U"chào"));
        check("hasWord([đc]) got [true] expected [false] (prefix is not a word)", !dictionary.hasWord(U"đc"));

        for (const std::u32string_view prefix : {U"đ", U"đc", U"c", U"ch", U"chà"}) {
            const std::string name = "hasWordOrPrefix([" + lotus::utf8::encode(prefix) + "]) got [false] expected [true]";
            check(name.c_str(), dictionary.hasWordOrPrefix(prefix));
        }
        for (const std::u32string_view miss : {U"đct", U"khoawjm", U""}) {
            const std::string name = "hasWordOrPrefix([" + lotus::utf8::encode(miss) + "]) got [true] expected [false]";
            check(name.c_str(), !dictionary.hasWordOrPrefix(miss));
        }

        // Go prints d.words when it reports a miss: the empty word is dropped and the rest is sorted
        // by code point ("chào" before "đcu", the same order as the byte-wise sort of Go).
        check("dictionary words got [chào đcu] expected [chào đcu]", dictionary.words() == std::vector<std::u32string>{U"chào", U"đcu"});

        const lotus::Dictionary nilDictionary;
        check("nil dictionary must not report a hit", !nilDictionary.hasWord(U"đcu") && !nilDictionary.hasWordOrPrefix(U"đ"));
    }

    // loadFromText lowercases every line (Go's NewDictionary reads the file and applies
    // bytes.ToLower), so the uppercase file entry is found through the lowercase lookup.
    void case_dictionary_load_from_text_lowercases() {
        lotus::Dictionary dictionary;
        dictionary.loadFromText("Đường\n");

        check("loadFromText([Đường]) got [missing] expected [found]", dictionary.hasWord(U"đường"));
        check("loadFromText([Đường]) got [missing] expected [found] as a prefix", dictionary.hasWordOrPrefix(U"đườ"));
        check("loadFromText([Đường]) stores [đường]", dictionary.words() == std::vector<std::u32string>{U"đường"});
        const std::vector<std::u32string>& stored    = dictionary.words();
        const std::u32string_view          firstWord = stored.empty() ? std::u32string_view() : std::u32string_view(stored.front());
        checkU32("loadFromText([Đường]) stores the lowercased word", firstWord, U"đường");
        check("the uppercase spelling is not stored", !dictionary.hasWord(U"Đường"));
    }

    // Empty lines are skipped, like Go's `if len(line) == 0 { continue }`, so they never reach the
    // sorted word list.
    void case_dictionary_load_from_text_skips_empty_lines() {
        lotus::Dictionary dictionary;
        dictionary.loadFromText("\n\nĐường\n\nCHÀO\n\n\n");

        check("empty lines are dropped", dictionary.words() == std::vector<std::u32string>{U"chào", U"đường"});
        check("hasWord(chào) after skipping empty lines", dictionary.hasWord(U"chào"));
        check("hasWord(đường) after skipping empty lines", dictionary.hasWord(U"đường"));

        lotus::Dictionary emptyFile;
        emptyFile.loadFromText("");
        check("an empty file loads no word", emptyFile.words().empty());

        lotus::Dictionary noTrailingNewline;
        noTrailingNewline.loadFromText("BÒ");
        check("a last line without a newline is loaded", noTrailingNewline.hasWord(U"bò"));
    }

} // namespace

int run_macro_dictionary() {
    case_macro_table_empty();
    case_macro_table_get();
    case_dictionary_lookup();
    case_dictionary_load_from_text_lowercases();
    case_dictionary_load_from_text_skips_empty_lines();

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus macro dictionary: all checks passed\n");
    return 0;
}
