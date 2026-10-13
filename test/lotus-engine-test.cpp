// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of every Test* function of bamboo/bamboo-core/bamboo_test.go, in file order, plus a replay of
// the whole test/golden/engine_core.tsv corpus that the Go engine dumped itself. Every mode, input
// and expectation is copied verbatim from the Go tests (or from the golden file); nothing is hand
// guessed. Column semantics of the corpus are documented in bamboo/golden_dump_test.go: the "mode"
// column is only the mode handed to ProcessKey, while processedHex is always
// GetProcessedString(VietnameseMode | FullText) (mask 33) and isValid is IsValid(true).
#include <optional>
#include "engine.h"

#include "input_method.h"
#include "types.h"
#include "utf8.h"

#include "golden-reader.h"

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

    using lotus::Engine;
    using lotus::Mode;

    // Mode masks of the Go tests: VietnameseMode, EnglishMode and VietnameseMode | FullText.
    constexpr lotus::ModeSet kVietnamese         = lotus::toMask(Mode::VietnameseMode);
    constexpr lotus::ModeSet kEnglish            = lotus::toMask(Mode::EnglishMode);
    constexpr lotus::ModeSet kVietnameseFullText = lotus::toMask(Mode::VietnameseMode) | lotus::toMask(Mode::FullText);

    int                      failures = 0;

    // Engine flags of the Go tests: EstdFlags without the W2U / std tone style bit.
    lotus::EngineFlags withoutW2u() {
        lotus::EngineFlags flags = lotus::kEstdFlags;
        return flags.remove(lotus::EngineFlag::W2uEnabled);
    }

    lotus::EngineFlags withoutStdToneStyle() {
        lotus::EngineFlags flags = lotus::kEstdFlags;
        return flags.remove(lotus::EngineFlag::StdToneStyle);
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

    void checkU32(const char* name, std::u32string_view actual, std::u32string_view expected) {
        if (actual == expected) {
            return;
        }
        std::printf("FAIL %s: got [%s], expected [%s]\n", name, hexOf(actual).c_str(), hexOf(expected).c_str());
        ++failures;
    }

    void checkValid(const char* name, Engine& engine, bool inputIsFullComplete, bool expected) {
        const bool actual = engine.isValid(inputIsFullComplete);
        if (actual != expected) {
            std::printf("FAIL %s: got %s, expected %s\n", name, actual ? "true" : "false", expected ? "true" : "false");
            ++failures;
        }
    }

    // Go's newStdEngine: Telex + EstdFlags + BracketTransformEverywhere.
    Engine newStdEngine() {
        Engine engine(lotus::cachedInputMethod("Telex"), lotus::kEstdFlags);
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        return engine;
    }

    // Go's GetProcessedString(mode).
    std::u32string processed(Engine& engine, lotus::ModeSet modeMask) {
        return engine.processedString(modeMask);
    }

    // Go's ProcessString(text, mode).
    void type(Engine& engine, std::u32string_view keys, lotus::ModeSet modeMask) {
        engine.processString(keys, modeMask);
    }

    void case_process_string() {
        Engine engine = newStdEngine();
        type(engine, U"aw", kVietnamese);
        checkU32("Process [aw]", processed(engine, kVietnamese), U"ă");
        engine.reset();
        type(engine, U"uw", kVietnamese);
        type(engine, U"o", kVietnamese);
        type(engine, U"w", kVietnamese);
        checkU32("Process [uwow]", processed(engine, kVietnamese), U"ươ");
        engine.reset();
        type(engine, U"chuaarn", kVietnamese);
        checkU32("Process [chuaarn]", processed(engine, kVietnamese), U"chuẩn");
        engine.reset();
        type(engine, U"giamaf", kVietnamese);
        checkU32("Process [giamaf]", processed(engine, kVietnamese), U"giầm");
    }

    void case_process_dd_string() {
        Engine engine = newStdEngine();
        type(engine, U"dd", kVietnamese);
        checkValid("IsSpellingCorrect [dd]", engine, false, true);
        engine.reset();
        type(engine, U"ddafi", kVietnamese);
        checkU32("Process [ddafi]", processed(engine, kVietnamese), U"đài");
    }

    void case_process_muoiwq_string() {
        Engine engine = newStdEngine();
        type(engine, U"Muoiwq", kVietnamese);
        checkU32("Process [Muoiwq]", processed(engine, kEnglish), U"Muoiwq");
        engine.reset();
        type(engine, U"mootj", kVietnamese);
        checkU32("Process [mootj]", processed(engine, kVietnamese), U"một");
    }

    void case_process_thuow_string() {
        Engine engine = newStdEngine();
        type(engine, U"Thuow", kVietnamese);
        checkU32("Process [Thuow]", processed(engine, kVietnamese), U"Thuơ");
        engine.removeLastChar(true);
        checkU32("Process [Thuow] and remove last char", processed(engine, kVietnamese), U"Thu");
    }

    void case_bamboo_engine_remove_last_char() {
        Engine engine = newStdEngine();
        engine.removeLastChar(true);
        type(engine, U" ", kEnglish);
        engine.removeLastChar(true);
        type(engine, U"loanj", kVietnamese);
        checkU32("Process [loanj]", processed(engine, kVietnamese), U"loạn");
        engine.removeLastChar(true);
        checkU32("Process [loanj-1]", processed(engine, kVietnamese), U"lọa");
        type(engine, U":", kEnglish);
        engine.removeLastChar(true);
        checkU32("Process [loanj-1]", processed(engine, kVietnamese), U"lọa");
    }

    void case_process_upper_string() {
        Engine engine = newStdEngine();
        type(engine, U"VIEETJ", kVietnamese);
        checkU32("Process [VIEETJ]", processed(engine, kVietnamese), U"VIỆT");
        engine.removeLastChar(false);
        checkU32("Process remove last char of upper string", processed(engine, kVietnamese), U"VIỆ");
        engine.processKey(U'Q', kVietnamese);
        checkU32("Process remove last char of upper string", processed(engine, kEnglish), U"VIEEJQ");
        engine.reset();
        type(engine, U"IB", kEnglish);
        checkU32("Process remove last char of upper string", processed(engine, kEnglish), U"IB");
    }

    void case_spelling_check() {
        Engine engine = newStdEngine();
        type(engine, U"noww", kVietnamese);
        checkU32("Process-ENG [noww]", processed(engine, kEnglish), U"noww");
        checkU32("Process-VIE [noww]", processed(engine, kVietnamese), U"now");
        engine.reset();
        type(engine, U"sawss", kVietnamese);
        checkU32("Process-ENG [sawss]", processed(engine, kEnglish), U"sawss");
        engine.reset();
        type(engine, U"sawss", kVietnamese);
        checkU32("Process-VIE [sawss]", processed(engine, kVietnamese), U"săs");
    }

    void case_process_dd() {
        Engine engine = newStdEngine();
        type(engine, U"dd", kVietnamese);
        checkValid("Check spelling for [dd]", engine, false, true);
        checkU32("Process [dd]", processed(engine, kVietnamese), U"đ");
        engine.reset();
        type(engine, U"SD", kVietnamese);
        type(engine, U"D", kVietnamese);
        checkU32("IsSpellingCorrect [SDD]", processed(engine, kVietnamese), U"SĐ");
    }

    void case_telex23() {
        Engine engine = newStdEngine();
        type(engine, U"t ]", kEnglish);
        type(engine, U"a", kVietnamese);
        checkU32("Process t ]a", processed(engine, kVietnamese), U"]a");
        engine.reset();
        type(engine, U"]]a", kVietnamese);
        checkU32("Process ]aa", processed(engine, kVietnamese), U"]a");

        Engine bracketEngine(lotus::cachedInputMethod("Telex"), lotus::kEstdFlags);
        bracketEngine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        type(bracketEngine, U"[", kVietnamese);
        checkU32("Process BracketTransform [[]", processed(bracketEngine, kVietnamese), U"ơ");
        bracketEngine.reset();
        type(bracketEngine, U"{", kVietnamese);
        checkU32("Process BracketTransform [{]", processed(bracketEngine, kVietnamese), U"Ơ");
    }

    void case_process_nguwowfi_string() {
        Engine engine = newStdEngine();
        type(engine, U"wowfi", kVietnamese);
        checkU32("Process [wowfi]", processed(engine, kVietnamese), U"ười");
    }

    void case_remove_last_char() {
        Engine engine = newStdEngine();
        type(engine, U"hanhj", kVietnamese);
        engine.removeLastChar(true);
        checkU32("Process [hanhj]", processed(engine, kVietnamese), U"hạn");
        engine.reset();
    }

    void case_process_catr_string() {
        Engine engine = newStdEngine();
        type(engine, U"catr", kVietnamese);
        checkU32("Process [catr]", processed(engine, kVietnamese), U"catr");
    }

    void case_process_toowi_string() {
        Engine engine = newStdEngine();
        type(engine, U"toowi", kVietnamese);
        checkU32("Process [toowi]", processed(engine, kVietnamese), U"tơi");
    }

    void case_process_aloo_string() {
        Engine engine = newStdEngine();
        type(engine, U"aloo", kVietnamese);
        checkU32("Process [aloo]", processed(engine, kVietnamese), U"alô");
    }

    // Go spells this case TestSpellingCheckForGiư; the identifier loses the Vietnamese rune.
    void case_spelling_check_for_giu() {
        Engine engine = newStdEngine();
        // With BracketTransform, ']' is used for 'ư'.
        type(engine, U"gi]", kVietnamese);
        checkValid("Process gi]", engine, false, true);
    }

    void case_double_brackets() {
        Engine engine = newStdEngine();
        type(engine, U"[[", kVietnamese);
        checkU32("TestDoubleBrackets", processed(engine, kEnglish), U"[");
    }

    void case_double_bracketso() {
        Engine engine = newStdEngine();
        type(engine, U"tooss", kVietnamese);
        checkU32("Process tooss", processed(engine, kVietnamese), U"tôs");
        engine.reset();
        type(engine, U"tosos", kVietnamese);
        checkU32("Process tosos", processed(engine, kVietnamese), U"tôs");
    }

    void case_double_w() {
        Engine engine = newStdEngine();
        type(engine, U"ww", kVietnamese);
        checkU32("TestDoubleW-ENG", processed(engine, kEnglish), U"w");
        checkU32("TestDoubleW-VIE", processed(engine, kVietnamese), U"w");
    }

    void case_double_w2() {
        Engine engine = newStdEngine();
        type(engine, U"wiw", kVietnamese);
        checkU32("TestDoubleW-VIE wiw", processed(engine, kVietnamese), U"uiw");
        checkU32("TestDoubleW-ENG wiw", processed(engine, kEnglish), U"wiw");
    }

    void case_process_duwoi() {
        Engine engine = newStdEngine();
        type(engine, U"duwoi", kVietnamese);
        checkU32("Process duwoi", processed(engine, kVietnamese), U"dươi");
    }

    void case_process_refresh() {
        Engine engine = newStdEngine();
        type(engine, U"reff", kVietnamese);
        type(engine, U"resh", kEnglish);
        checkU32("Process-ENG [reff+resh]", processed(engine, kEnglish), U"reffresh");
        checkU32("Process-VIE [reff+resh]", processed(engine, kVietnamese), U"refresh");
    }

    void case_process_refresh2() {
        Engine engine = newStdEngine();
        type(engine, U"reff", kVietnamese);
        engine.removeLastChar(true);
        engine.processKey(U'f', kVietnamese);
        checkU32("Process reff-1+f", processed(engine, kVietnamese), U"rè");
    }

    void case_process_dd_seq() {
        Engine engine = newStdEngine();
        type(engine, U"oddp", kVietnamese);
        checkU32("Process oddp", processed(engine, kVietnamese), U"ođp");
    }

    void case_process_gisa() {
        Engine engine = newStdEngine();
        type(engine, U"gis", kVietnamese);
        type(engine, U"a", kVietnamese);
        checkU32("Process gisa", processed(engine, kVietnamese), U"giá");
    }

    void case_process_kimso() {
        Engine engine = newStdEngine();
        type(engine, U"kimso", kVietnamese);
        checkU32("TestProcessKimso", processed(engine, kVietnamese), U"kímo");
    }

    void case_process_to() {
        Engine engine = newStdEngine();
        type(engine, U"to", kVietnamese);
        checkValid("Process to", engine, true, true);
    }

    void case_process_toorr() {
        Engine engine = newStdEngine();
        type(engine, U"toorr", kVietnamese);
        checkU32("Process toorr", processed(engine, kVietnamese), U"tôr");
    }

    void case_process_tnoss() {
        Engine engine = newStdEngine();
        type(engine, U"tnoss", kVietnamese);
        checkU32("Process tnoss", processed(engine, kVietnamese), U"tnos");
    }

    void case_process_eenghf() {
        Engine engine(lotus::cachedInputMethod("Telex"), lotus::kEstdFlags);
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        type(engine, U"ddawks", kVietnamese);
        checkU32("Process eenghf", processed(engine, kVietnamese), U"đắk");
    }

    void case_process_hieeur() {
        Engine engine = newStdEngine();
        type(engine, U"tooi oo HIEEUR", kVietnamese);
        checkU32("Process [tooi oo HIEEUR]", processed(engine, kVietnamese), U"HIỂU");
    }

    void case_process_nguoiw() {
        Engine engine = newStdEngine();
        type(engine, U"NGUOIW", kVietnamese);
        checkU32("TestProcessToorr", processed(engine, kVietnamese), U"NGƯƠI");
    }

    void case_process_tos() {
        Engine engine = newStdEngine();
        type(engine, U"{s", kVietnamese);
        checkU32("Process {+s", processed(engine, kVietnamese), U"Ớ");
    }

    void case_process_to5() {
        Engine engine(lotus::cachedInputMethod("VNI"), lotus::kEstdFlags);
        type(engine, U"o55", kVietnamese);
        checkU32("Process [o55-VNI]", processed(engine, kVietnamese), U"o5");
    }

    void case_process_huoswc() {
        Engine engine = newStdEngine();
        type(engine, U"duwongwj", kVietnamese);
        checkU32("Process [duwongwj]", processed(engine, kVietnamese), U"duongwj");
    }

    void case_process_choas() {
        Engine engine(lotus::cachedInputMethod("Telex"), withoutStdToneStyle());
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        type(engine, U"choas", kVietnamese);
        checkU32("Process [choas]", processed(engine, kVietnamese), U"choá");
        engine.reset();
        type(engine, U"bieecs", kVietnamese);
        checkU32("Process [bieecs]", processed(engine, kVietnamese), U"biếc");
        engine.reset();
        type(engine, U"uese", kVietnamese);
        checkU32("Process uese", processed(engine, kVietnamese), U"uế");
    }

    void case_bamboo_engine_restore_last_word() {
        Engine engine = newStdEngine();
        type(engine, U"duwongj tooi", kVietnamese);
        engine.restoreLastWord(false);
        checkU32("Process [duwongwj tooi]", processed(engine, kVietnamese), U"tooi");
    }

    void case_bamboo_engine_restore_last_word_tcvn() {
        Engine engine(lotus::cachedInputMethod("Microsoft layout"), lotus::kEstdFlags);
        type(engine, U"112", kVietnamese);
        checkU32("Process-VIE 112 (Microsoft layout)", processed(engine, kVietnamese), U"1â");
        engine.restoreLastWord(false);
        checkU32("Process-ENG 112 (Microsoft layout)", processed(engine, kEnglish), U"12");
        engine.reset();
        type(engine, U"d[]ng9 t4i", kVietnamese);
        engine.restoreLastWord(false);
        checkU32("Process [duongwj t4i - MS layout]", processed(engine, kVietnamese), U"t4i");
    }

    void case_bamboo_engine_zprocessing() {
        Engine engine = newStdEngine();
        type(engine, U"loz", kVietnamese);
        checkU32("Process loz", processed(engine, kVietnamese), U"loz");
        engine.reset();
        type(engine, U"losz", kVietnamese);
        checkU32("Process-VIE losz", processed(engine, kVietnamese), U"lo");
        checkU32("Process-ENG losz", processed(engine, kEnglish), U"losz");
    }

    // The Go test only logs its result, there is no expectation to port.
    void case_restore_last_word() {
        Engine engine = newStdEngine();
        type(engine, U"afq", kVietnamese);
        engine.restoreLastWord(false);
        engine.removeLastChar(true);
        engine.processKey(U'f', kVietnamese);
    }

    void case_process_vn_word() {
        Engine engine = newStdEngine();
        type(engine, U"tôifs", kVietnamese);
        checkU32("Process tôifs", processed(engine, kVietnamese), U"tối");
        checkU32("Process-ENG tôifs", processed(engine, kEnglish), U"tôifs");
        engine.reset();
        type(engine, U"tốif", kVietnamese);
        checkU32("Process tốif", processed(engine, kVietnamese), U"tồi");
        checkU32("Process-ENG tốif", processed(engine, kEnglish), U"tốif");
        engine.reset();
        type(engine, U"tốiz", kVietnamese);
        checkU32("Process tốiz", processed(engine, kVietnamese), U"tôi");
    }

    void case_double_typing() {
        Engine engine = newStdEngine();
        type(engine, U"linux", kVietnamese);
        type(engine, U"x", kVietnamese);
        checkU32("Process [linuxx]", processed(engine, kVietnamese), U"linux");
        engine.reset();
        type(engine, U"buwo", kVietnamese);
        type(engine, U"o", kVietnamese);
        checkU32("Process [buwoo]", processed(engine, kVietnamese), U"buô");
        engine.reset();
        type(engine, U"buowc", kVietnamese);
        type(engine, U"o", kVietnamese);
        checkU32("Process [buowco]", processed(engine, kVietnamese), U"buôc");
        engine.reset();
        type(engine, U"cuoiw", kVietnamese);
        type(engine, U"o", kVietnamese);
        checkU32("Process [cuoiwo]", processed(engine, kVietnamese), U"cuôi");
        engine.reset();
        type(engine, U"ach", kVietnamese);
        type(engine, U"a", kVietnamese);
        checkU32("Process [acha]", processed(engine, kVietnamese), U"acha");
        engine.reset();
        type(engine, U"nhuw", kVietnamese);
        checkU32("Process [nhuw]", processed(engine, kVietnamese), U"như");
        checkValid("Findresultmatch full", engine, true, true);
        // AddDictionaryToSpellingTrie(map[string]bool{"thứ": true})
        engine.reset();
        type(engine, U"thuw", kVietnamese);
        checkValid("thuw is valid", engine, true, true);
        engine.reset();
        type(engine, U"thow", kVietnamese);
        checkValid("thow is valid", engine, true, true);
        engine.reset();
        // AddDictionaryToSpellingTrie(map[string]bool{"tôi": true, "tối": true, "tời": true, "tơi": true})
        type(engine, U"tooi", kVietnamese);
        checkU32("Process [tooi]", processed(engine, kVietnamese), U"tôi");
        checkValid("Findresultmatch full", engine, true, true);
        engine.reset();
        type(engine, U"arch", kVietnamese);
        checkValid("false arch", engine, false, false);
        engine.reset();
        type(engine, U"[[", kVietnamese);
        type(engine, U"oo", kVietnamese);
        checkU32("Process [oo", processed(engine, kVietnamese), U"[ô");
        engine.reset();
        type(engine, U"oo]", kVietnamese);
        checkU32("Process oo]", processed(engine, kVietnamese), U"ôư");
        engine.reset();
        type(engine, U"chury", kVietnamese);
        checkValid("IsValid chury", engine, true, true);
        engine.reset();
        type(engine, U"turyn", kVietnamese);
        engine.removeLastChar(true);
        engine.removeLastChar(true);
        // engine.ProcessString("r", VietnameseMode)
        checkU32("Process turyen,BS,BS,BS,r", processed(engine, kVietnamese), U"tủ");
        engine.reset();
        type(engine, U"chuyển", kVietnamese);
        type(engine, U"z", kVietnamese);
        checkU32("Process [chuyểnz]", processed(engine, kVietnamese), U"chuyên");
        engine.reset();
        type(engine, U"nhueej", kVietnamese);
        checkU32("Process nhueej", processed(engine, kVietnamese), U"nhuệ");
        engine.reset();
        type(engine, U"cuongw", kVietnamese);
        checkU32("Process cuongw", processed(engine, kVietnamese), U"cương");
        engine.reset();
        type(engine, U"quawcj", kVietnamese);
        checkU32("Process quawcj", processed(engine, kVietnamese), U"quặc");
        // eechs
        engine.reset();
        type(engine, U"quawcj", kVietnamese);
        checkU32("Process quawcj", processed(engine, kVietnamese), U"quặc");
        engine.reset();
        type(engine, U"tôi）t", kEnglish);
        checkU32("Process [tôi）t]", processed(engine, kVietnamese), U"t");
        engine.reset();
    }

    void case_ew2u_enabled() {
        // Standard Telex (no w -> ư rule)
        const lotus::InputMethod im = lotus::cachedInputMethod("Telex");

        // Flag OFF
        Engine engine(lotus::cachedInputMethod("Telex"), withoutW2u());
        engine.processKey(U'w', kVietnamese);
        checkU32("Ew2uEnabled OFF: Process [w]", processed(engine, kVietnamese), U"w");

        // Flag ON - Start of word
        engine = Engine(im, lotus::kEstdFlags | lotus::EngineFlag::W2uEnabled);
        engine.processKey(U'w', kVietnamese);
        checkU32("Ew2uEnabled ON (start): Process [w]", processed(engine, kVietnamese), U"ư");

        // Flag ON - Not start of word (bw)
        engine.reset();
        engine.processKey(U'b', kVietnamese);
        engine.processKey(U'w', kVietnamese);
        checkU32("Ew2uEnabled ON (not start): Process [bw]", processed(engine, kVietnamese), U"bư");

        // Flag ON - Uppercase start of word
        engine.reset();
        engine.processKey(U'W', kVietnamese);
        checkU32("Ew2uEnabled ON (upper start): Process [W]", processed(engine, kVietnamese), U"Ư");
    }

    void case_free_cv_spelling() {
        // Onset-vowel pairing is free (isValidCV removed); the rime rule (isValidVC) is kept.
        // Previously blocked by cvMatrix: b/m/n x oă.
        const auto checkCase = [](Engine& engine, std::u32string_view typed, std::u32string_view want, bool wantValid) {
            engine.reset();
            engine.processString(typed, kVietnamese);
            const std::u32string got = engine.processedString(kVietnamese);
            if (got != want) {
                std::printf("FAIL Process [%s], got [%s], expected [%s]\n", hexOf(typed).c_str(), hexOf(got).c_str(), hexOf(want).c_str());
                ++failures;
            }
            const bool valid = engine.isValid(true);
            if (valid != wantValid) {
                std::printf("FAIL IsValid(true) for [%s]: got %s, expected %s\n", hexOf(typed).c_str(), valid ? "true" : "false", wantValid ? "true" : "false");
                ++failures;
            }
        };

        // w2u ON (default EstdFlags): previously garbled into "boaựm".
        Engine engine = newStdEngine();
        checkCase(engine, U"boawjm", U"boặm", true);
        checkCase(engine, U"moawsm", U"moắm", true);
        checkCase(engine, U"noawfm", U"noằm", true);
        // Regression: words that already worked keep working.
        checkCase(engine, U"khoawsm", U"khoắm", true);
        checkCase(engine, U"loawjm", U"loặm", true);
        // Rime rule preserved: "a" + "k" is not a valid rime.
        checkCase(engine, U"tak", U"tak", false);
        // Rime "ă" + "k" stays valid (vowel-row union, unchanged).
        checkCase(engine, U"tawsk", U"tắk", true);

        // w2u OFF: previously stayed raw "boawjm".
        engine = Engine(lotus::cachedInputMethod("Telex"), withoutW2u());
        checkCase(engine, U"boawjm", U"boặm", true);
    }

    void case_bracket_transform_global() {
        Engine engine(lotus::cachedInputMethod("Telex"), lotus::kEstdFlags);
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);

        type(engine, U"[", kVietnamese);
        checkU32("BracketTransform (Everywhere): [", processed(engine, kVietnameseFullText), U"ơ");
        engine.reset();
        type(engine, U"]", kVietnamese);
        checkU32("BracketTransform (Everywhere): ]", processed(engine, kVietnameseFullText), U"ư");

        engine.reset();
        engine.setBracketTransformMode(lotus::BracketTransformMode::NonStart);
        type(engine, U"[", kVietnamese);
        // At start of word, [ should stay [.
        checkU32("BracketTransform (NonStart) at start: [", processed(engine, kVietnameseFullText), U"[");
        engine.reset();
        type(engine, U"a[", kVietnamese);
        // After a, [ should become ơ.
        checkU32("BracketTransform (NonStart) after a: [", processed(engine, kVietnameseFullText), U"aơ");

        // Test double typing to cancel.
        engine.reset();
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        type(engine, U"[[", kVietnamese);
        checkU32("BracketTransform (Everywhere) double typing: [[", processed(engine, kVietnameseFullText), U"[");
    }

    void case_bracket_transform_with_tone() {
        Engine engine(lotus::cachedInputMethod("Telex"), lotus::kEstdFlags);
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);

        // Test m[f -> mờ
        type(engine, U"m[f", kVietnamese);
        checkU32("BracketTransform + Tone: m[f", processed(engine, kVietnamese), U"mờ");

        // Test d]s -> dứ
        engine.reset();
        type(engine, U"d]s", kVietnamese);
        checkU32("BracketTransform + Tone: d]s", processed(engine, kVietnamese), U"dứ");
    }

    void case_bracket_disabled_with_tone() {
        Engine engine(lotus::cachedInputMethod("Telex"), lotus::kEstdFlags);
        engine.setBracketTransformMode(lotus::BracketTransformMode::Disabled);

        // Test m[f -> m[f (since [ is literal and not a vowel, f remains f)
        type(engine, U"m[f", kVietnamese);
        checkU32("Bracket Disabled + Tone: m[f", processed(engine, kVietnameseFullText), U"m[f");
    }

    // Replays test/golden/engine_core.tsv: one engine per case id, built from the flags/w2u/bracket
    // of its first row, then one ProcessKey per step. Returns the number of rows checked.
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
        const std::string                                        path = std::string(goldenDir()) + "/engine_core.tsv";
        const auto                                               rows = readGolden(path);

        std::unordered_map<std::string, std::unique_ptr<Engine>> engines;
        long                                                     checked    = 0;
        int                                                      mismatches = 0;
        int                                                      shown      = 0;
        for (const auto& row : rows) {
            const std::string& id = row.fields[0];
            auto               it = engines.find(id);
            if (it == engines.end()) {
                auto engine = std::make_unique<Engine>(lotus::cachedInputMethod("Telex"), lotus::engineFlagsFromBits(static_cast<uint32_t>(std::stoul(row.fields[4]))));
                engine->setW2uMode(w2uModeOf(row.fields[5]));
                engine->setBracketTransformMode(bracketTransformModeOf(row.fields[6]));
                it = engines.emplace(id, std::move(engine)).first;
            }
            Engine&              engine = *it->second;
            const auto           key    = static_cast<char32_t>(hexValue(row.fields[2]));
            const lotus::ModeSet mode   = lotus::modeSetFromBits(static_cast<uint32_t>(std::stoul(row.fields[3])));
            engine.processKey(key, mode);
            // The dumper always renders with VietnameseMode | FullText (mask 33); the mode column is
            // only the mode handed to ProcessKey.
            const std::string actual    = lotus::utf8::encode(engine.processedString(kVietnameseFullText));
            const std::string expected  = fromHex(row.fields[7]);
            const bool        valid     = engine.isValid(true);
            const bool        wantValid = row.fields[8] == "1";
            ++checked;
            if (actual != expected || valid != wantValid) {
                ++mismatches;
                if (shown < 20) {
                    ++shown;
                    std::printf("FAIL golden %s step %s key U+%04X mode %u: processed=[%s], expected=[%s], valid=%d, want=%d\n", id.c_str(), row.fields[1].c_str(),
                                static_cast<unsigned>(key), mode.bits, actual.c_str(), expected.c_str(), valid ? 1 : 0, wantValid ? 1 : 0);
                }
            }
        }
        std::printf("checked=%ld mismatches=%d\n", checked, mismatches);
        failures += mismatches;
        return checked;
    }

} // namespace

int run_engine() {
    case_process_string();
    case_process_dd_string();
    case_process_muoiwq_string();
    case_process_thuow_string();
    case_bamboo_engine_remove_last_char();
    case_process_upper_string();
    case_spelling_check();
    case_process_dd();
    case_telex23();
    case_process_nguwowfi_string();
    case_remove_last_char();
    case_process_catr_string();
    case_process_toowi_string();
    case_process_aloo_string();
    case_spelling_check_for_giu();
    case_double_brackets();
    case_double_bracketso();
    case_double_w();
    case_double_w2();
    case_process_duwoi();
    case_process_refresh();
    case_process_refresh2();
    case_process_dd_seq();
    case_process_gisa();
    case_process_kimso();
    case_process_to();
    case_process_toorr();
    case_process_tnoss();
    case_process_eenghf();
    case_process_hieeur();
    case_process_nguoiw();
    case_process_tos();
    case_process_to5();
    case_process_huoswc();
    case_process_choas();
    case_bamboo_engine_restore_last_word();
    case_bamboo_engine_restore_last_word_tcvn();
    case_bamboo_engine_zprocessing();
    case_restore_last_word();
    case_process_vn_word();
    case_double_typing();
    case_ew2u_enabled();
    case_free_cv_spelling();
    case_bracket_transform_global();
    case_bracket_transform_with_tone();
    case_bracket_disabled_with_tone();
    const long goldenRows = replayGolden();

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus engine: all checks passed (46 cases + golden %ld rows)\n", goldenRows);
    return 0;
}
