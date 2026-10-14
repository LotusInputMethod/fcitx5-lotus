// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of every Test* function of bamboo/bamboo-core/rebuild_test.go, in file order. Every input,
// expectation and table entry is copied verbatim from the Go tests; TestXxx became case_xxx(). The
// last case is an extra one that checks the rebuild path against the key stroke path of the engine.
#include "engine.h"

#include "flattener.h"
#include "input_method.h"
#include "rebuild.h"
#include "types.h"
#include "utf8.h"

#include <array>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

    using lotus::Engine;
    using lotus::Mode;

    // Mode masks of the Go tests: VietnameseMode and VietnameseMode | FullText.
    constexpr lotus::ModeSet kVietnamese         = lotus::toMask(Mode::VietnameseMode);
    constexpr lotus::ModeSet kVietnameseFullText = lotus::toMask(Mode::VietnameseMode) | lotus::toMask(Mode::FullText);

    // Go's anonymous `tests` struct of the tables that only carry input and expectation.
    struct RebuildCase {
        std::u32string_view input;
        std::u32string_view want;
    };

    // Go's anonymous `tests` struct of the tables that also carry a subtest name.
    struct NamedRebuildCase {
        const char*         name;
        std::u32string_view input;
        std::u32string_view want;
    };

    int failures = 0;

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

    // Go's RebuildCompositionFromText(text, stdStyle) followed by Flatten(composition, VietnameseMode).
    std::u32string rebuilt(std::u32string_view text, bool stdStyle) {
        return lotus::flatten(lotus::rebuildCompositionFromText(text, stdStyle), kVietnamese);
    }

    // Go's NewEngine(ParseInputMethod(GetInputMethodDefinitions(), "Telex"), EstdFlags).
    Engine newTelexEngine() {
        return {lotus::cachedInputMethod(lotus::InputMethodId::Telex), lotus::kEstdFlags};
    }

    // Go's t.Run(tt.input, ...) subtest name, kept as "RebuildFromText('<input>')".
    std::string testName(std::u32string_view input) {
        return "RebuildFromText('" + lotus::utf8::encode(input) + "')";
    }

    void case_rebuild_from_text_simple_ascii() {
        const std::u32string result = rebuilt(U"goo", true);
        checkU32("RebuildFromText('goo')", result, U"goo");
    }

    void case_rebuild_from_text_simple_vietnamese() {
        const std::array<RebuildCase, 10> tests = {
            RebuildCase{.input = U"chào", .want = U"chào"},   RebuildCase{.input = U"việt", .want = U"việt"},   RebuildCase{.input = U"google", .want = U"google"},
            RebuildCase{.input = U"đường", .want = U"đường"}, RebuildCase{.input = U"người", .want = U"người"}, RebuildCase{.input = U"as", .want = U"as"},
            RebuildCase{.input = U"được", .want = U"được"},   RebuildCase{.input = U"những", .want = U"những"}, RebuildCase{.input = U"ước", .want = U"ước"},
            RebuildCase{.input = U"ươi", .want = U"ươi"},
        };

        for (const RebuildCase& test : tests) {
            const std::u32string result = rebuilt(test.input, true);
            const std::string    name   = testName(test.input);
            checkU32(name.c_str(), result, test.want);
        }
    }

    void case_rebuild_from_text_upper_case() {
        const std::array<RebuildCase, 4> tests = {
            RebuildCase{.input = U"Việt", .want = U"Việt"},
            RebuildCase{.input = U"OO", .want = U"OO"},
            RebuildCase{.input = U"DD", .want = U"DD"},
            RebuildCase{.input = U"Nội", .want = U"Nội"},
        };

        for (const RebuildCase& test : tests) {
            const std::u32string result = rebuilt(test.input, true);
            const std::string    name   = testName(test.input);
            checkU32(name.c_str(), result, test.want);
        }
    }

    void case_rebuild_from_text_then_process_key() {
        Engine engine = newTelexEngine();

        engine.rebuildFromText(U"go");
        checkU32("After rebuild 'go'", engine.processedString(kVietnamese), U"go");

        engine.processKey(U's', kVietnamese);
        checkU32("After rebuild 'go' + ProcessKey('s')", engine.processedString(kVietnamese), U"gó");
    }

    void case_rebuild_from_text_compare_with_process_string() {
        Engine engine = newTelexEngine();

        engine.processString(U"goo", kVietnamese);
        const std::u32string buggyResult = engine.processedString(kVietnamese);

        engine.reset();
        engine.rebuildFromText(U"goo");
        const std::u32string correctResult = engine.processedString(kVietnamese);

        // Go only logs when ProcessString already returns "goo" (t.Log) and always logs both results
        // (t.Logf); both are informational, so the port keeps only the unexpected-case message.
        if (buggyResult == U"goo") {
            std::printf("note: ProcessString('goo') unexpectedly correct - Telex behavior may have changed\n");
        }
        checkU32("RebuildFromText('goo')", correctResult, U"goo");
    }

    void case_rebuild_from_text_with_punctuation_and_space() {
        const std::array<NamedRebuildCase, 13> tests = {
            NamedRebuildCase{.name = "vãi", .input = U"vãi", .want = U"vãi"},          NamedRebuildCase{.name = "vãi.", .input = U"vãi.", .want = U"vãi."},
            NamedRebuildCase{.name = "vãi ", .input = U"vãi ", .want = U"vãi "},       NamedRebuildCase{.name = "vãi. ", .input = U"vãi. ", .want = U"vãi. "},
            NamedRebuildCase{.name = "chào.", .input = U"chào.", .want = U"chào."},    NamedRebuildCase{.name = "chào. ", .input = U"chào. ", .want = U"chào. "},
            NamedRebuildCase{.name = "chào, ", .input = U"chào, ", .want = U"chào, "}, NamedRebuildCase{.name = "chào. Xin", .input = U"chào. Xin", .want = U"chào. Xin"},
            NamedRebuildCase{.name = "vãi, ", .input = U"vãi, ", .want = U"vãi, "},    NamedRebuildCase{.name = "vãi! ", .input = U"vãi! ", .want = U"vãi! "},
            NamedRebuildCase{.name = "vãi? ", .input = U"vãi? ", .want = U"vãi? "},    NamedRebuildCase{.name = "vãi; ", .input = U"vãi; ", .want = U"vãi; "},
            NamedRebuildCase{.name = "vãi: ", .input = U"vãi: ", .want = U"vãi: "},
        };

        for (const NamedRebuildCase& test : tests) {
            const std::u32string result = rebuilt(test.input, true);
            checkU32(test.name, result, test.want);
        }
    }

    // Test RebuildEngineFromText with punctuation and space.
    void case_rebuild_engine_from_text_with_punctuation_and_space() {
        Engine                                engine = newTelexEngine();

        const std::array<NamedRebuildCase, 6> tests = {
            NamedRebuildCase{.name = "vãi", .input = U"vãi", .want = U"vãi"},       NamedRebuildCase{.name = "vãi.", .input = U"vãi.", .want = U"vãi."},
            NamedRebuildCase{.name = "vãi ", .input = U"vãi ", .want = U"vãi "},    NamedRebuildCase{.name = "vãi. ", .input = U"vãi. ", .want = U"vãi. "},
            NamedRebuildCase{.name = "chào.", .input = U"chào.", .want = U"chào."}, NamedRebuildCase{.name = "chào. ", .input = U"chào. ", .want = U"chào. "},
        };

        for (const NamedRebuildCase& test : tests) {
            engine.rebuildFromText(test.input);
            checkU32(test.name, engine.processedString(kVietnameseFullText), test.want);
        }
    }

    // Rebuilds a text with one engine and types the same text with another one: the rebuild path has
    // to agree with the key stroke path, and both have to give the text back unchanged.
    void case_rebuild_matches_process_string() {
        const std::array<std::u32string_view, 5> texts = {U"chào", U"việt", U"đường", U"người", U"chào. Xin"};

        for (const std::u32string_view text : texts) {
            Engine rebuiltEngine = newTelexEngine();
            rebuiltEngine.rebuildFromText(text);

            Engine typedEngine = newTelexEngine();
            typedEngine.processString(text, kVietnameseFullText);

            const std::u32string rebuiltResult = rebuiltEngine.processedString(kVietnameseFullText);
            const std::string    name          = lotus::utf8::encode(text);
            checkU32(("rebuild '" + name + "'").c_str(), rebuiltResult, text);
            checkU32(("rebuild vs typing '" + name + "'").c_str(), rebuiltResult, typedEngine.processedString(kVietnameseFullText));
        }
    }

} // namespace

int run_rebuild() {
    case_rebuild_from_text_simple_ascii();
    case_rebuild_from_text_simple_vietnamese();
    case_rebuild_from_text_upper_case();
    case_rebuild_from_text_then_process_key();
    case_rebuild_from_text_compare_with_process_string();
    case_rebuild_from_text_with_punctuation_and_space();
    case_rebuild_engine_from_text_with_punctuation_and_space();
    case_rebuild_matches_process_string();

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus rebuild: all checks passed\n");
    return 0;
}
