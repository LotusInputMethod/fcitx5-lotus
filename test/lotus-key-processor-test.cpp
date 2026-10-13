// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of every Test* function of bamboo/fcitxbambooengine_test.go (24 tests) and of
// bamboo/options_test.go (7 tests), in file order, plus a replay of the whole
// test/golden/key_processor.tsv corpus that the Go wrapper dumped itself. Every input, modifier mask,
// option and expectation is copied verbatim from the Go tests or from the golden file. Go's
// newTestEngine()/typeKeys() build a FcitxBambooEngine around a cgo handle; the port builds the
// KeyProcessor directly and feeds it the keysym of each rune.
//
// Two Go fields have no counterpart: `w2u` (never read by the engine, so TestEnglishNumberW2UInMacroMode
// only keeps its assertions) and `shouldRestoreKeyStrokes`, which is exposed through a setter because
// the Go test drives it directly. `formatTime`/`expandMacro`/`updatePreedit`/`toUpper`/
// `mustFallbackToEnglish`/`shouldFallbackToEnglish`/`commitPreeditAndReset` are called by the ported
// tests, so they are public on the C++ class (Go has no visibility rules).
#include "key_processor.h"

#include "dictionary.h"
#include "encoder.h"
#include "input_method.h"
#include "macro_table.h"
#include "types.h"
#include "utf8.h"

#include "golden-reader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

    using lotus::KeyProcessor;
    using lotus::MacroCase;

    // Option bits of the golden corpus, from bamboo/golden_dump_test.go (optAutoNonVnRestore, ...).
    constexpr uint32_t kOptAutoNonVnRestore    = 1U << 0U;
    constexpr uint32_t kOptDdFreeStyle         = 1U << 1U;
    constexpr uint32_t kOptSpellCheckWithDicts = 1U << 2U;
    constexpr uint32_t kOptMacroEnabled        = 1U << 3U;

    int                failures = 0;

    // Engine flags of the Go tests: EstdFlags without the W2U / std tone style / free tone marking bit.
    lotus::EngineFlags withoutW2u() {
        lotus::EngineFlags flags = lotus::kEstdFlags;
        return flags.remove(lotus::EngineFlag::W2uEnabled);
    }

    lotus::EngineFlags withoutStdToneStyle() {
        lotus::EngineFlags flags = lotus::kEstdFlags;
        return flags.remove(lotus::EngineFlag::StdToneStyle);
    }

    lotus::EngineFlags withoutFreeToneMarking() {
        lotus::EngineFlags flags = lotus::kEstdFlags;
        return flags.remove(lotus::EngineFlag::FreeToneMarking);
    }

    // Renders a code point list as "U+XXXX U+YYYY", used to report mismatching expectations.
    std::string hexOf(std::u32string_view text) {
        std::string          out;
        std::array<char, 16> buffer{};
        for (const char32_t codePoint : text) {
            std::snprintf(buffer.data(), buffer.size(), "%sU+%04X", out.empty() ? "" : " ", static_cast<unsigned int>(codePoint));
            out += buffer.data();
        }
        return out;
    }

    // Renders a byte string as lowercase hex, used to report golden mismatches.
    std::string toHex(std::string_view bytes) {
        std::string         out;
        std::array<char, 4> buffer{};
        for (const char byte : bytes) {
            std::snprintf(buffer.data(), buffer.size(), "%02x", static_cast<unsigned char>(byte));
            out += buffer.data();
        }
        return out;
    }

    void checkU32(const char* name, std::u32string_view actual, std::u32string_view expected) {
        if (actual == expected) {
            return;
        }
        std::printf("FAIL %s: got [%s], expected [%s]\n", name, hexOf(actual).c_str(), hexOf(expected).c_str());
        ++failures;
    }

    void checkStr(const char* name, std::string_view actual, std::string_view expected) {
        if (actual == expected) {
            return;
        }
        std::printf("FAIL %s: got [%s], expected [%s]\n", name, std::string(actual).c_str(), std::string(expected).c_str());
        ++failures;
    }

    void checkBool(const char* name, bool actual, bool expected) {
        if (actual == expected) {
            return;
        }
        std::printf("FAIL %s: got %s, expected %s\n", name, actual ? "true" : "false", expected ? "true" : "false");
        ++failures;
    }

    // Go's newTestEngine(dict, spellCheckWithDicts).
    KeyProcessor newTestEngine(const std::vector<std::string>& dict, bool spellCheckWithDicts) {
        KeyProcessor engine(lotus::cachedInputMethod("Telex"), spellCheckWithDicts);
        auto         table = std::make_shared<lotus::MacroTable>();
        engine.setMacroTable(std::move(table));
        auto dictionary = std::make_shared<lotus::Dictionary>();
        dictionary->loadFromWords(dict);
        engine.setDictionary(std::move(dictionary));
        return engine;
    }

    // Go's newOptEngine(flags, dict, spellCheckWithDicts).
    KeyProcessor newOptEngine(lotus::EngineFlags flags, const std::vector<std::string>& dict, bool spellCheckWithDicts) {
        KeyProcessor engine = newTestEngine(dict, spellCheckWithDicts);
        engine.engine().setFlag(flags);
        return engine;
    }

    // Go's typeKeys: every rune of keys is typed with no modifier.
    void typeKeys(KeyProcessor& engine, std::string_view keys) {
        for (const char32_t key : lotus::utf8::decode(keys)) {
            engine.processKey(key, {});
        }
    }

    // std::regex is not allowed in the engine, so the shapes the Go tests match with regexp are
    // checked by hand.
    bool isDigit(char32_t codePoint) {
        return codePoint >= U'0' && codePoint <= U'9';
    }

    // ^\d{2}:\d{2}$
    bool hasTimeShape(std::u32string_view text) {
        return text.size() == 5 && isDigit(text[0]) && isDigit(text[1]) && text[2] == U':' && isDigit(text[3]) && isDigit(text[4]);
    }

    // ^\d{2}/\d{2}/\d{4}$
    bool hasDateShape(std::u32string_view text) {
        return text.size() == 10 && isDigit(text[0]) && isDigit(text[1]) && text[2] == U'/' && isDigit(text[3]) && isDigit(text[4]) && text[5] == U'/' && isDigit(text[6]) &&
            isDigit(text[7]) && isDigit(text[8]) && isDigit(text[9]);
    }

    // \d
    bool containsDigit(std::u32string_view text) {
        return std::ranges::any_of(text, isDigit);
    }

    // ^\d{2}:\d{2} giờ$
    bool hasTimeMacroShape(std::u32string_view text) {
        return text.size() == 9 && hasTimeShape(text.substr(0, 5)) && text[5] == U' ' && text[6] == U'g' && text[7] == U'i' && text[8] == U'ờ';
    }

    // TestDetermineMacroCase.
    void case_determine_macro_case() {
        struct Case {
            std::u32string_view in;
            MacroCase           want;
        };
        const std::vector<Case> cases = {
            {.in = U"abc", .want = MacroCase::AllSmall},  {.in = U"ABC", .want = MacroCase::AllCapital}, {.in = U"aBc", .want = MacroCase::NoChange},
            {.in = U"123", .want = MacroCase::NoChange},  {.in = U"", .want = MacroCase::NoChange},      {.in = U"áộ", .want = MacroCase::AllSmall},
            {.in = U"ÁỘ", .want = MacroCase::AllCapital},
        };
        for (const Case& c : cases) {
            const MacroCase got = lotus::determineMacroCase(c.in);
            if (got != c.want) {
                std::printf("FAIL determineMacroCase(%s): got [%d], expected [%d]\n", hexOf(c.in).c_str(), static_cast<int>(got), static_cast<int>(c.want));
                ++failures;
            }
        }
    }

    // TestGetLastRune.
    void case_get_last_rune() {
        checkBool("getLastRune(\"\") got [0] expected [0]", lotus::getLastRune(U"") == 0, true);
        checkBool("getLastRune(abc) got [c] expected [c]", lotus::getLastRune(U"abc") == U'c', true);
        checkBool("getLastRune(tắk) got [k] expected [k]", lotus::getLastRune(U"tắk") == U'k', true);
    }

    // TestInKeyList.
    void case_in_key_list() {
        const std::vector<char32_t> list = {U'a', U'w', U's'};
        checkBool("inKeyList([a w s], a) got [false] expected [true]", lotus::inKeyList(list, U'a'), true);
        checkBool("inKeyList([a w s], z) got [true] expected [false]", lotus::inKeyList(list, U'z'), false);
        checkBool("inKeyList(nil, a) got [true] expected [false]", lotus::inKeyList({}, U'a'), false);
    }

    // TestFormatTime.
    void case_format_time() {
        KeyProcessor engine = newTestEngine({}, false);
        checkStr("formatTime(\"\") got [empty] expected [empty]", lotus::KeyProcessor::formatTime(""), "");
        checkStr("formatTime(no placeholders) got [same] expected [same]", lotus::KeyProcessor::formatTime("no placeholders"), "no placeholders");
        checkBool("formatTime(%H:%M) got [HH:MM] expected shape HH:MM", hasTimeShape(lotus::utf8::decode(lotus::KeyProcessor::formatTime("%H:%M"))), true);
        checkBool("formatTime(%d/%m/%Y) got [DD/MM/YYYY] expected shape DD/MM/YYYY", hasDateShape(lotus::utf8::decode(lotus::KeyProcessor::formatTime("%d/%m/%Y"))), true);
        const std::string fallback = lotus::KeyProcessor::formatTime("%Q");
        checkBool("formatTime(%Q) got [empty] expected fallback different from input", !fallback.empty() && fallback != "%Q", true);
    }

    // TestExpandMacro.
    void case_expand_macro() {
        KeyProcessor engine = newTestEngine({}, false);
        // autoCapitalize off: macro returned as-is regardless of trigger case
        checkU32("expandMacro no-cap got [Việt Nam] expected [Việt Nam]", engine.expandMacro(U"VN", U"Việt Nam"), U"Việt Nam");
        // autoCapitalize on, trigger all-lowercase -> lowercase macro
        engine.options().autoCapitalizeMacro = true;
        checkU32("expandMacro all-small got [việt nam] expected [việt nam]", engine.expandMacro(U"vn", U"Việt Nam"), U"việt nam");
        checkU32("expandMacro all-capital got [VIỆT NAM] expected [VIỆT NAM]", engine.expandMacro(U"VN", U"việt nam"), U"VIỆT NAM");
        checkU32("expandMacro mixed got [Việt Nam] expected [Việt Nam] (unchanged)", engine.expandMacro(U"Vn", U"Việt Nam"), U"Việt Nam");
        engine.options().autoCapitalizeMacro = false;
        // $TIME substitution
        engine.options().timeFormat = "%H:%M";
        checkBool("expandMacro $TIME got [digits] expected to contain digits", containsDigit(engine.expandMacro(U"now", U"giờ $TIME")), true);
        // $TIME untouched when timeFormat is empty
        engine.options().timeFormat = "";
        checkU32("expandMacro $TIME empty-format got [giờ $TIME] expected literal [$TIME]", engine.expandMacro(U"now", U"giờ $TIME"), U"giờ $TIME");
        // $DATE substitution
        engine.options().dateFormat = "%d/%m/%Y";
        checkBool("expandMacro $DATE got [/] expected to contain [/]", engine.expandMacro(U"now", U"$DATE").find(U'/') != std::u32string::npos, true);
    }

    // TestUpdatePreeditClearsBoth: the Go test starts from a struct literal with preeditText "x" and
    // commitText "y"; the port reaches the same state through the public API (a word break leaves a
    // pending commit, typing after it leaves both texts set).
    void case_update_preedit_clears_both() {
        KeyProcessor engine = newTestEngine({}, false);
        typeKeys(engine, "chao");
        engine.processKey(lotus::kFcitxSpace, {});
        typeKeys(engine, "aw");
        checkU32("state before updatePreedit(\"\") got [ă] expected [ă]", engine.preedit(), U"ă");
        engine.updatePreedit(U"");
        checkBool("updatePreedit(\"\") left preedit non-empty", engine.preedit().empty(), true);
        checkBool("updatePreedit(\"\") left commit non-empty", engine.takeCommit().empty(), true);
    }

    // TestEncodeCharsets: bamboo.Encode(cs, "tôi") is the identity for these charsets (they are
    // either the Unicode table or unknown names, which Go and the port both pass through).
    void case_encode_charsets() {
        for (const std::string_view cs : {"Unicode", "VNI", "TCVN3"}) {
            checkStr("Encode(cs, tôi) got [tôi] expected [tôi]", lotus::encode(cs, U"tôi"), "tôi");
        }
    }

    // The charset step of updatePreedit/takeCommit: the Unicode charset keeps the code points, a
    // legacy charset still decodes the bytes it produces back to code points (Go keeps the encoded
    // bytes in a string). test/golden/charset.tsv maps U+0111 to c2ae for "TCVN3 (ABC)", i.e. '®'.
    void case_encode_text_charset() {
        KeyProcessor unicodeEngine = newTestEngine({}, false);
        typeKeys(unicodeEngine, "dd");
        checkU32("Unicode preedit got [đ] expected [đ]", unicodeEngine.preedit(), U"đ");
        unicodeEngine.commitPreedit();
        checkU32("Unicode commit got [đ] expected [đ]", unicodeEngine.takeCommit(), U"đ");

        KeyProcessor legacyEngine            = newTestEngine({}, false);
        legacyEngine.options().outputCharset = "TCVN3 (ABC)";
        typeKeys(legacyEngine, "dd");
        checkU32("TCVN3 (ABC) preedit got [®] expected [®]", legacyEngine.preedit(), U"®");
        legacyEngine.commitPreedit();
        checkU32("TCVN3 (ABC) commit got [®] expected [®]", legacyEngine.takeCommit(), U"®");
    }

    // TestPreeditTelexComposition.
    void case_preedit_telex_composition() {
        struct Case {
            std::string_view    keys;
            std::u32string_view want;
        };
        const std::vector<Case> cases = {
            {.keys = "aw", .want = U"ă"},
            {.keys = "dd", .want = U"đ"},
            {.keys = "tooi", .want = U"tôi"},
            {.keys = "chaof", .want = U"chào"},
        };
        for (const Case& c : cases) {
            KeyProcessor engine = newTestEngine({}, false);
            typeKeys(engine, c.keys);
            const std::string name = "type [" + std::string(c.keys) + "] preedit";
            checkU32(name.c_str(), engine.preedit(), c.want);
        }
    }

    // TestWordBreakCommit.
    void case_word_break_commit() {
        KeyProcessor engine = newTestEngine({}, false);
        typeKeys(engine, "chao");
        checkBool("space on [chao] returned [false] expected [true]", engine.processKey(lotus::kFcitxSpace, {}), true);
        checkU32("commit got [chao ] expected [chao ]", engine.takeCommit(), U"chao ");
        checkBool("preedit after commit got [empty] expected [empty]", engine.preedit().empty(), true);
    }

    // TestAutoRestoreDictMode.
    void case_auto_restore_dict_mode() {
        // In dict -> kept Vietnamese
        KeyProcessor engine = newTestEngine({"chào"}, true);
        typeKeys(engine, "chaof");
        checkU32("preedit got [chào] expected [chào]", engine.preedit(), U"chào");
        engine.processKey(lotus::kFcitxSpace, {});
        checkU32("dict-hit commit got [chào ] expected [chào ]", engine.takeCommit(), U"chào ");

        // Not in dict -> restored to raw keystrokes at word break
        engine = newTestEngine({"chào"}, true);
        typeKeys(engine, "khoawjm");
        checkU32("preedit got [khoặm] expected [khoặm]", engine.preedit(), U"khoặm");
        engine.processKey(lotus::kFcitxSpace, {});
        checkU32("dict-miss commit got [khoawjm ] expected [khoawjm ] (raw restore)", engine.takeCommit(), U"khoawjm ");
    }

    // TestAutoRestoreRulesMode.
    void case_auto_restore_rules_mode() {
        KeyProcessor engine = newTestEngine({}, false);
        // rules-invalid sequence -> must fall back
        typeKeys(engine, "tak");
        checkBool("mustFallbackToEnglish for [tak] got [false] expected [true] (rules-invalid)", engine.mustFallbackToEnglish(), true);
        // rules-valid word -> keep
        engine = newTestEngine({}, false);
        typeKeys(engine, "tooi");
        checkBool("mustFallbackToEnglish for [tooi] got [true] expected [false]", engine.mustFallbackToEnglish(), false);
        // full flow: rules-valid rime survives word break (cv-free bamboo-core)
        engine = newTestEngine({}, false);
        typeKeys(engine, "boawjm");
        checkU32("preedit got [boặm] expected [boặm]", engine.preedit(), U"boặm");
        engine.processKey(lotus::kFcitxSpace, {});
        checkU32("rules-valid commit got [boặm ] expected [boặm ]", engine.takeCommit(), U"boặm ");
    }

    // TestDDFreeStyle.
    void case_dd_free_style() {
        KeyProcessor engine = newTestEngine({}, true);
        typeKeys(engine, "dd");
        checkBool("ddFreeStyle on: mustFallbackToEnglish for [dd] got [true] expected [false]", engine.mustFallbackToEnglish(), false);
        engine.options().ddFreeStyle = false;
        checkBool("ddFreeStyle off: mustFallbackToEnglish for [dd] got [false] expected [true]", engine.mustFallbackToEnglish(), true);
    }

    // TestAutoNonVnRestoreDisabled.
    void case_auto_non_vn_restore_disabled() {
        KeyProcessor engine               = newTestEngine({}, true);
        engine.options().autoNonVnRestore = false;
        typeKeys(engine, "boawjm");
        engine.processKey(lotus::kFcitxSpace, {});
        checkU32("autoNonVnRestore disabled commit got [boặm ] expected [boặm ] (never restore)", engine.takeCommit(), U"boặm ");
    }

    // TestToUpper.
    void case_to_upper() {
        KeyProcessor engine = newTestEngine({}, false);
        // letters untouched; brackets inactive by default
        checkBool("toUpper(b) got [b] expected [b]", engine.toUpper(U'b') == U'b', true);
        checkBool("toUpper([) without bracket mode got [[] expected [[]", engine.toUpper(U'[') == U'[', true);
        // brackets active: [ -> {, ] -> }, { -> [, } -> ]
        engine.engine().setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        struct Case {
            char32_t in;
            char32_t want;
        };
        const std::vector<Case> cases = {
            {.in = U'[', .want = U'{'},
            {.in = U']', .want = U'}'},
            {.in = U'{', .want = U'['},
            {.in = U'}', .want = U']'},
        };
        for (const Case& c : cases) {
            const char32_t got = engine.toUpper(c.in);
            if (got != c.want) {
                std::printf("FAIL toUpper(%s) with bracket mode: got [U+%04X], expected [U+%04X]\n", hexOf(std::u32string(1, c.in)).c_str(), static_cast<unsigned int>(got),
                            static_cast<unsigned int>(c.want));
                ++failures;
            }
        }
    }

    // TestShiftSpaceRestoreKeyStrokes.
    void case_shift_space_restore_key_strokes() {
        KeyProcessor engine = newTestEngine({}, false);
        typeKeys(engine, "aw");
        checkU32("preedit got [ă] expected [ă]", engine.preedit(), U"ă");
        engine.setShouldRestoreKeyStrokes(true);
        engine.processKey(lotus::kFcitxSpace, {});
        checkBool("shouldRestoreKeyStrokes not consumed", engine.shouldRestoreKeyStrokes(), false);
        checkU32("preedit after shift+space got [aw] expected [aw] (raw keystrokes)", engine.preedit(), U"aw");
    }

    // TestBackspace.
    void case_backspace() {
        // single rune -> commit empty and reset
        KeyProcessor engine = newTestEngine({}, false);
        typeKeys(engine, "aw");
        checkBool("backspace on single rune returned [false] expected [true]", engine.processKey(lotus::kFcitxBackSpace, {}), true);
        checkBool("backspace single rune left commit non-empty", engine.takeCommit().empty(), true);
        checkBool("backspace single rune left preedit non-empty", engine.preedit().empty(), true);

        // multi rune -> remove last char
        engine = newTestEngine({}, false);
        typeKeys(engine, "chao");
        engine.processKey(lotus::kFcitxBackSpace, {});
        checkU32("backspace on [chao] preedit got [cha] expected [cha]", engine.preedit(), U"cha");

        // empty engine -> rejected
        engine = newTestEngine({}, false);
        checkBool("backspace on empty engine returned [true] expected [false]", engine.processKey(lotus::kFcitxBackSpace, {}), false);
    }

    // TestTabMacroExpansion.
    void case_tab_macro_expansion() {
        KeyProcessor engine = newTestEngine({}, false);
        engine.setMacroEnabled(true);
        auto table = std::make_shared<lotus::MacroTable>();
        table->set("vn", "Việt Nam");
        engine.setMacroTable(std::move(table));
        typeKeys(engine, "vn");
        checkBool("tab with macro match returned [false] expected [true]", engine.processKey(lotus::kFcitxTab, {}), true);
        checkU32("tab macro commit got [Việt Nam] expected [Việt Nam]", engine.takeCommit(), U"Việt Nam");

        // no macro match -> commit composed text, reject
        engine = newTestEngine({}, false);
        engine.setMacroEnabled(true);
        table = std::make_shared<lotus::MacroTable>();
        table->set("vn", "Việt Nam");
        engine.setMacroTable(std::move(table));
        typeKeys(engine, "xx");
        checkBool("tab without macro match returned [true] expected [false]", engine.processKey(lotus::kFcitxTab, {}), false);
        checkU32("tab no-match commit got [xx] expected [xx]", engine.takeCommit(), U"xx");
    }

    // TestTabTimeMacro.
    void case_tab_time_macro() {
        KeyProcessor engine = newTestEngine({}, false);
        engine.setMacroEnabled(true);
        // Macro keys match the composed text: "now" composes to "nơ" (o+w -> ơ).
        auto table = std::make_shared<lotus::MacroTable>();
        table->set("nơ", "$TIME giờ");
        engine.setMacroTable(std::move(table));
        engine.options().timeFormat = "%H:%M";
        typeKeys(engine, "now");
        engine.processKey(lotus::kFcitxTab, {});
        checkBool("tab time macro commit got [HH:MM giờ] expected shape [HH:MM giờ]", hasTimeMacroShape(engine.takeCommit()), true);
    }

    // TestCanProcessKey.
    void case_can_process_key() {
        KeyProcessor engine = newTestEngine({}, false);
        for (const uint32_t kv : {lotus::kFcitxSpace, lotus::kFcitxBackSpace, static_cast<uint32_t>(U',')}) {
            checkBool("canProcessKey got [false] expected [true]", engine.canProcessKey(kv), true);
        }
        for (const char32_t r : std::u32string_view(U"aw")) {
            checkBool("canProcessKey got [false] expected [true]", engine.canProcessKey(r), true);
        }
        // Non-ASCII non-letter: not alpha, not punctuation, not a Vietnamese rune.
        checkBool("canProcessKey(€) got [true] expected [false]", engine.canProcessKey(static_cast<uint32_t>(U'€')), false);
    }

    // TestIsValidState.
    void case_is_valid_state() {
        checkBool("isValidState(0) got [false] expected [true]", KeyProcessor::isValidState({}), true);
        checkBool("isValidState(Shift) got [false] expected [true] (shift is allowed)", KeyProcessor::isValidState(lotus::KeyState::Shift), true);
        for (const lotus::KeyState mask :
             {lotus::KeyState::Ctrl, lotus::KeyState::Mod1, lotus::KeyState::Ignored, lotus::KeyState::Super, lotus::KeyState::Hyper, lotus::KeyState::Meta}) {
            checkBool("isValidState(mask) got [true] expected [false]", KeyProcessor::isValidState(mask), false);
        }

        // A rejected modifier must let the key through instead of swallowing it into the preedit.
        KeyProcessor blockedEngine = newTestEngine({}, false);
        checkBool("a with Meta held got [true] expected [false]", blockedEngine.processKey(U'a', lotus::KeyState::Meta), false);
        checkBool("preedit after a with Meta held non-empty", blockedEngine.preedit().empty(), true);
    }

    // TestEnglishNumberW2UInMacroMode (Go also sets the never-read `w2u` field there).
    void case_english_number_w2u_in_macro_mode() {
        KeyProcessor engine = newTestEngine({}, false);
        engine.setMacroEnabled(true);

        typeKeys(engine, "qwen2");
        checkU32("type [qwen2] preedit got [qwen2] expected [qwen2]", engine.preedit(), U"qwen2");

        engine.commitPreeditAndReset(U"");

        typeKeys(engine, "new1");
        checkU32("type [new1] preedit got [new1] expected [new1]", engine.preedit(), U"new1");
    }

    // TestAutoRestoreDictPrefixKeepsPartialWord.
    void case_auto_restore_dict_prefix_keeps_partial_word() {
        for (const bool ddFreeStyle : {true, false}) {
            KeyProcessor engine          = newTestEngine({"đcu"}, true);
            engine.options().ddFreeStyle = ddFreeStyle;

            typeKeys(engine, "ddc");
            checkU32("preedit after [ddc] got [đc] expected [đc] (dictionary prefix)", engine.preedit(), U"đc");

            typeKeys(engine, "u");
            checkU32("preedit after [ddcu] got [đcu] expected [đcu] (dictionary hit)", engine.preedit(), U"đcu");

            engine.processKey(lotus::kFcitxSpace, {});
            checkU32("commit got [đcu ] expected [đcu ]", engine.takeCommit(), U"đcu ");
        }
    }

    // TestAutoRestoreDictPrefixStillRestoresIncompleteWord.
    void case_auto_restore_dict_prefix_still_restores_incomplete_word() {
        KeyProcessor engine          = newTestEngine({"đcu"}, true);
        engine.options().ddFreeStyle = false;

        typeKeys(engine, "ddc");
        engine.processKey(lotus::kFcitxSpace, {});
        checkU32("commit got [ddc ] expected [ddc ] (incomplete word restored)", engine.takeCommit(), U"ddc ");
    }

    // TestSpellCheckWithDictsAcceptsDictionaryPrefix.
    void case_spell_check_with_dicts_accepts_dictionary_prefix() {
        KeyProcessor engine          = newTestEngine({"đcu"}, true);
        engine.options().ddFreeStyle = false;

        typeKeys(engine, "ddc");
        checkBool("shouldFallbackToEnglish for [đc] with dictionary prefix got [true] expected [false]", engine.shouldFallbackToEnglish(true), false);

        typeKeys(engine, "u");
        checkBool("shouldFallbackToEnglish for [đcu] with dictionary hit got [true] expected [false]", engine.shouldFallbackToEnglish(true), false);
        checkBool("mustFallbackToEnglish for [đcu] with dictionary hit got [true] expected [false]", engine.mustFallbackToEnglish(), false);
    }

    // TestW2UModes.
    void case_w2u_modes() {
        struct Case {
            std::string_view              name;
            lotus::EngineFlags            flags;
            std::optional<lotus::W2uMode> mode; // nullopt = don't call setW2uMode
            std::string_view              keys;
            std::u32string_view           want;
        };
        const std::vector<Case> cases = {
            {.name = "default", .flags = lotus::kEstdFlags, .mode = std::nullopt, .keys = "w", .want = U"ư"},
            {.name = "default-bw", .flags = lotus::kEstdFlags, .mode = std::nullopt, .keys = "bw", .want = U"bư"},
            {.name = "flag-off", .flags = withoutW2u(), .mode = std::nullopt, .keys = "w", .want = U"w"},
            {.name = "flag-off-bw", .flags = withoutW2u(), .mode = std::nullopt, .keys = "bw", .want = U"bw"},
            {.name = "disabled", .flags = lotus::kEstdFlags, .mode = lotus::W2uMode::Disabled, .keys = "w", .want = U"w"},
            {.name = "nonstart", .flags = lotus::kEstdFlags, .mode = lotus::W2uMode::NonStart, .keys = "w", .want = U"w"},
            {.name = "nonstart-bw", .flags = lotus::kEstdFlags, .mode = lotus::W2uMode::NonStart, .keys = "bw", .want = U"bư"},
            {.name = "everywhere", .flags = lotus::kEstdFlags, .mode = lotus::W2uMode::Everywhere, .keys = "w", .want = U"ư"},
        };
        for (const Case& c : cases) {
            KeyProcessor engine = newOptEngine(c.flags, {}, false);
            engine.engine().setW2uMode(c.mode);
            typeKeys(engine, c.keys);
            const std::string name = "w2u " + std::string(c.name) + ": type [" + std::string(c.keys) + "] preedit";
            checkU32(name.c_str(), engine.preedit(), c.want);
        }
        // "aw" -> ă takes precedence over w->ư on the default engine.
        KeyProcessor engine = newOptEngine(lotus::kEstdFlags, {}, false);
        typeKeys(engine, "aw");
        checkU32("w2u default: type [aw] preedit got [ă] expected [ă]", engine.preedit(), U"ă");
    }

    // TestOptionMatrixAutoRestoreSpellCheck.
    void case_option_matrix_auto_restore_spell_check() {
        const std::vector<std::string> dict = {"chào"};
        struct Combo {
            bool                autoRestore;
            bool                withDicts;
            std::u32string_view wantChaofCommit;
            std::u32string_view wantBoawjm;
        };
        const std::vector<Combo> matrix = {
            {.autoRestore = true, .withDicts = true, .wantChaofCommit = U"chào ", .wantBoawjm = U"boawjm "},
            {.autoRestore = true, .withDicts = false, .wantChaofCommit = U"chào ", .wantBoawjm = U"boặm "},
            {.autoRestore = false, .withDicts = true, .wantChaofCommit = U"chào ", .wantBoawjm = U"boặm "},
            {.autoRestore = false, .withDicts = false, .wantChaofCommit = U"chào ", .wantBoawjm = U"boặm "},
        };
        for (const Combo& c : matrix) {
            KeyProcessor engine               = newTestEngine(dict, c.withDicts);
            engine.options().autoNonVnRestore = c.autoRestore;
            typeKeys(engine, "chaof");
            engine.processKey(lotus::kFcitxSpace, {});
            const std::string name = "matrix auto=" + std::string(c.autoRestore ? "true" : "false") + " dict=" + std::string(c.withDicts ? "true" : "false") + ": [chaof] commit";
            checkU32(name.c_str(), engine.takeCommit(), c.wantChaofCommit);

            engine                            = newTestEngine(dict, c.withDicts);
            engine.options().autoNonVnRestore = c.autoRestore;
            typeKeys(engine, "boawjm");
            engine.processKey(lotus::kFcitxSpace, {});
            const std::string boawjmName =
                "matrix auto=" + std::string(c.autoRestore ? "true" : "false") + " dict=" + std::string(c.withDicts ? "true" : "false") + ": [boawjm] commit";
            checkU32(boawjmName.c_str(), engine.takeCommit(), c.wantBoawjm);
        }
    }

    // TestSpellCheckWithDictsDirect.
    void case_spell_check_with_dicts_direct() {
        // dict hit (lowercase key as produced by NewDictionary) -> no fallback
        KeyProcessor engine = newTestEngine({"chào"}, true);
        typeKeys(engine, "chaof");
        checkBool("mustFallbackToEnglish for [chaof] with dict hit got [true] expected [false]", engine.mustFallbackToEnglish(), false);
        // dict miss -> fallback
        engine = newTestEngine({"chào"}, true);
        typeKeys(engine, "boawjm");
        checkBool("mustFallbackToEnglish for [boawjm] with dict miss got [false] expected [true]", engine.mustFallbackToEnglish(), true);
        // empty dict -> everything misses
        engine = newTestEngine({}, true);
        typeKeys(engine, "chaof");
        checkBool("mustFallbackToEnglish for [chaof] with empty dict got [false] expected [true]", engine.mustFallbackToEnglish(), true);
    }

    // TestAutoNonVnRestoreDirect.
    void case_auto_non_vn_restore_direct() {
        KeyProcessor engine               = newTestEngine({}, true);
        engine.options().autoNonVnRestore = false;
        typeKeys(engine, "boawjm");
        checkBool("autoNonVnRestore=false: mustFallbackToEnglish got [true] expected [false] (early return)", engine.mustFallbackToEnglish(), false);
        engine = newTestEngine({}, true);
        typeKeys(engine, "boawjm");
        checkBool("autoNonVnRestore=true: mustFallbackToEnglish got [false] expected [true]", engine.mustFallbackToEnglish(), true);
    }

    // TestModernStyle.
    void case_modern_style() {
        KeyProcessor engine = newOptEngine(lotus::kEstdFlags, {}, false);
        typeKeys(engine, "hoaf");
        checkU32("std tone style: hoaf preedit got [hòa] expected [hòa]", engine.preedit(), U"hòa");
        engine = newOptEngine(withoutStdToneStyle(), {}, false);
        typeKeys(engine, "hoaf");
        checkU32("modern tone style: hoaf preedit got [hoà] expected [hoà]", engine.preedit(), U"hoà");
    }

    // TestBracketTransform.
    void case_bracket_transform() {
        struct Case {
            std::string_view    keys;
            std::u32string_view want;
        };
        const std::vector<Case> cases = {
            {.keys = "[", .want = U"ơ"},
            {.keys = "t[", .want = U"tơ"},
            {.keys = "]", .want = U"ư"},
            {.keys = "t]", .want = U"tư"},
        };
        for (const Case& c : cases) {
            KeyProcessor engine = newTestEngine({}, false);
            engine.engine().setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
            typeKeys(engine, c.keys);
            const std::string name = "bracket: type [" + std::string(c.keys) + "] preedit";
            checkU32(name.c_str(), engine.preedit(), c.want);
        }
    }

    // TestFreeMarkingFlag.
    void case_free_marking_flag() {
        KeyProcessor engine = newTestEngine({}, false);
        checkBool("default EstdFlags: EfreeToneMarking missing from flags", engine.engine().flag().has(lotus::EngineFlag::FreeToneMarking), true);
        KeyProcessor modernEngine = newOptEngine(withoutFreeToneMarking(), {}, false);
        checkBool("EstdFlags&^EfreeToneMarking: flag still set", !modernEngine.engine().flag().has(lotus::EngineFlag::FreeToneMarking), true);
        modernEngine.engine().setFlag(lotus::kEstdFlags | lotus::EngineFlag::FreeToneMarking);
        checkBool("SetFlag(EstdFlags|EfreeToneMarking): flag not present", modernEngine.engine().flag().has(lotus::EngineFlag::FreeToneMarking), true);
    }

    // Replays test/golden/key_processor.tsv: one processor per case id, built from the opts/flags/
    // w2u/bracket of its first row exactly like bamboo/golden_dump_test.go dumpKeyProcessor does,
    // then one preeditProcessKeyEvent per step. Columns are case, step, keyHex, state, opts, flags,
    // w2u, bracket, accepted, preeditHex, commitHex; keyHex is a keysym and preeditHex/commitHex are
    // the UTF-8 bytes of preeditText / commitText (commitText cleared after each step, like the dump).
    // LOTUS_GOLDEN_DIR redirects the replay to a locally generated sweep; the compiled in
    // LOTUS_TEST_GOLDEN_DIR stays the default for the committed corpus.
    const char* goldenDir() {
        if (const char* override = std::getenv("LOTUS_GOLDEN_DIR")) {
            return override;
        }
        return LOTUS_TEST_GOLDEN_DIR;
    }

    // The corpus stores the modes as the integers of the Go API; -1 is the engine internal "follow the
    // flags" state, which is the state a fresh engine starts in, so the setter stays untouched.
    std::optional<lotus::W2uMode> w2uModeOf(const std::string& field) {
        switch (std::stoi(field)) {
            case 0: return lotus::W2uMode::Disabled;
            case 1: return lotus::W2uMode::NonStart;
            case 2: return lotus::W2uMode::Everywhere;
            default: return std::nullopt;
        }
    }

    std::optional<lotus::BracketTransformMode> bracketTransformModeOf(const std::string& field) {
        switch (std::stoi(field)) {
            case 0: return lotus::BracketTransformMode::Disabled;
            case 1: return lotus::BracketTransformMode::NonStart;
            case 2: return lotus::BracketTransformMode::Everywhere;
            default: return std::nullopt;
        }
    }

    long replayGolden() {
        const std::string                             path = std::string(goldenDir()) + "/key_processor.tsv";
        const auto                                    rows = readGolden(path);

        std::unordered_map<std::string, KeyProcessor> processors;
        long                                          checked    = 0;
        int                                           mismatches = 0;
        int                                           shown      = 0;
        for (const auto& row : rows) {
            const std::string& id = row.fields[0];
            auto               it = processors.find(id);
            if (it == processors.end()) {
                const auto   opts = static_cast<uint32_t>(std::stoul(row.fields[4]));
                KeyProcessor processor(lotus::cachedInputMethod("Telex"), (opts & kOptSpellCheckWithDicts) != 0);
                auto         dictionary = std::make_shared<lotus::Dictionary>();
                dictionary->loadFromWords({"vietnam", "nguoi", "duong"});
                processor.setDictionary(std::move(dictionary));
                auto table = std::make_shared<lotus::MacroTable>();
                if ((opts & kOptMacroEnabled) != 0) {
                    table->set("vn", "Việt Nam");
                    table->set("ko", "không");
                }
                processor.setMacroTable(std::move(table));
                processor.options().autoNonVnRestore = (opts & kOptAutoNonVnRestore) != 0;
                processor.options().ddFreeStyle      = (opts & kOptDdFreeStyle) != 0;
                processor.setMacroEnabled((opts & kOptMacroEnabled) != 0);
                processor.engine().setFlag(lotus::engineFlagsFromBits(static_cast<uint32_t>(std::stoul(row.fields[5]))));
                processor.engine().setW2uMode(w2uModeOf(row.fields[6]));
                processor.engine().setBracketTransformMode(bracketTransformModeOf(row.fields[7]));
                it = processors.emplace(id, std::move(processor)).first;
            }
            KeyProcessor&     processor     = it->second;
            const uint32_t    key           = hexValue(row.fields[2]);
            const auto        state         = lotus::keyStatesFromBits(static_cast<uint32_t>(std::stoul(row.fields[3])));
            const bool        accepted      = processor.processKey(key, state);
            const std::string actualPreedit = lotus::utf8::encode(processor.preedit());
            const std::string actualCommit  = lotus::utf8::encode(processor.takeCommit());
            const bool        wantAccepted  = row.fields[8] == "1";
            const std::string wantPreedit   = fromHex(row.fields[9]);
            const std::string wantCommit    = fromHex(row.fields[10]);
            ++checked;
            if (accepted != wantAccepted || actualPreedit != wantPreedit || actualCommit != wantCommit) {
                ++mismatches;
                if (shown < 20) {
                    ++shown;
                    std::printf("FAIL golden %s step %s key U+%04X state %u: accepted=%d/%d preedit=[%s]/[%s] commit=[%s]/[%s]\n", id.c_str(), row.fields[1].c_str(),
                                static_cast<unsigned>(key), state.bits, accepted ? 1 : 0, wantAccepted ? 1 : 0, toHex(actualPreedit).c_str(), toHex(wantPreedit).c_str(),
                                toHex(actualCommit).c_str(), toHex(wantCommit).c_str());
                }
            }
        }
        std::printf("checked=%ld mismatches=%d\n", checked, mismatches);
        failures += mismatches;
        return checked;
    }

} // namespace

int run_key_processor() {
    case_determine_macro_case();
    case_get_last_rune();
    case_in_key_list();
    case_format_time();
    case_expand_macro();
    case_update_preedit_clears_both();
    case_encode_charsets();
    case_encode_text_charset();
    case_preedit_telex_composition();
    case_word_break_commit();
    case_auto_restore_dict_mode();
    case_auto_restore_rules_mode();
    case_dd_free_style();
    case_auto_non_vn_restore_disabled();
    case_to_upper();
    case_shift_space_restore_key_strokes();
    case_backspace();
    case_tab_macro_expansion();
    case_tab_time_macro();
    case_can_process_key();
    case_is_valid_state();
    case_english_number_w2u_in_macro_mode();
    case_auto_restore_dict_prefix_keeps_partial_word();
    case_auto_restore_dict_prefix_still_restores_incomplete_word();
    case_spell_check_with_dicts_accepts_dictionary_prefix();
    case_w2u_modes();
    case_option_matrix_auto_restore_spell_check();
    case_spell_check_with_dicts_direct();
    case_auto_non_vn_restore_direct();
    case_modern_style();
    case_bracket_transform();
    case_free_marking_flag();
    const long goldenRows = replayGolden();

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus key processor: all checks passed (32 cases + golden %ld rows)\n", goldenRows);
    return 0;
}
