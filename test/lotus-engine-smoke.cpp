// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke checks of the ported engine core (step 9). The syllable level cases of bamboo_test.go are
// ported with the golden differential in step 10; here only a handful of end to end paths are driven
// by hand.
#include "engine.h"

#include "input_method.h"
#include "types.h"

#include <cstdio>
#include <string>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    void checkPreedit(const char* name, lotus::Engine& engine, std::u32string_view expected) {
        const std::u32string actual = engine.processedString(lotus::Mode::VietnameseMode | lotus::Mode::FullText);
        if (actual != expected) {
            std::printf("FAIL %s: got", name);
            for (const char32_t codePoint : actual) {
                std::printf(" U+%04X", static_cast<unsigned int>(codePoint));
            }
            std::printf(", expected");
            for (const char32_t codePoint : expected) {
                std::printf(" U+%04X", static_cast<unsigned int>(codePoint));
            }
            std::printf("\n");
            ++failures;
        }
    }

    void type(lotus::Engine& engine, std::u32string_view text) {
        engine.processString(text, lotus::toMask(lotus::Mode::VietnameseMode));
    }

    lotus::Engine newStdEngine() {
        return {lotus::cachedInputMethod("Telex"), lotus::kEstdFlags};
    }

} // namespace

int run_engine_smoke() {
    {
        lotus::Engine engine = newStdEngine();
        check("input method name", engine.inputMethod().name == "Telex");
        check("flags", engine.flag().bits == lotus::kEstdFlags.bits);
        type(engine, U"dd");
        checkPreedit("dd", engine, U"đ");
        check("dd is valid", engine.isValid(true));

        // Go's RemoveLastChar drops the last appending node together with every node that targets it,
        // so "dd" (the "d" node plus the mark that makes it "đ") loses both and the composition ends up
        // empty.
        engine.removeLastChar(true);
        checkPreedit("remove last char", engine, U"");
    }

    {
        lotus::Engine engine = newStdEngine();
        // curated-04 of the golden corpus: "duwowngf" is "dường" (a single "d" never becomes "đ").
        type(engine, U"duwowngf");
        checkPreedit("duwowngf", engine, U"dường");
        check("duwowngf is valid", engine.isValid(true));
    }

    {
        lotus::Engine engine = newStdEngine();
        type(engine, U"tieengs");
        checkPreedit("tieengs", engine, U"tiếng");
    }

    {
        lotus::Engine engine = newStdEngine();
        check("bracket key is not processed by default", !engine.canProcessKey(U'['));
        check("letter key is processed", engine.canProcessKey(U'w'));
        engine.setBracketTransformMode(lotus::BracketTransformMode::Everywhere);
        check("bracket key is processed when enabled", engine.canProcessKey(U'['));
        type(engine, U"d[");
        checkPreedit("bracket transform", engine, U"dơ");
    }

    {
        lotus::Engine engine = newStdEngine();
        type(engine, U"dd");
        check("composition size after dd", engine.composition().size() == 2U);
        engine.reset();
        checkPreedit("reset", engine, U"");
        // Go's isValid returns true for a composition of at most one node, empty included.
        check("empty engine is valid like Go", engine.isValid(true));
    }

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus engine smoke: all checks passed\n");
    return 0;
}
