// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sanity checks for the golden corpus: the files must exist, parse, and keep the column layout the
// differential tests rely on.
#include "golden-reader.h"

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

    // LOTUS_GOLDEN_DIR redirects the smoke check to a locally generated sweep; the compiled in
    // LOTUS_TEST_GOLDEN_DIR stays the default for the committed corpus.
    const char* goldenDir() {
        if (const char* override = std::getenv("LOTUS_GOLDEN_DIR")) {
            return override;
        }
        return LOTUS_TEST_GOLDEN_DIR;
    }

    std::string goldenPath(const char* name) {
        return std::string(goldenDir()) + "/" + name;
    }

} // namespace

int run_golden_smoke() {
    const auto charset = readGolden(goldenPath("charset.tsv"));
    const auto core    = readGolden(goldenPath("engine_core.tsv"));
    const auto wrapper = readGolden(goldenPath("key_processor.tsv"));

    check("charset rows", !charset.empty());
    check("engine core rows", !core.empty());
    check("key processor rows", !wrapper.empty());

    for (const auto& row : charset) {
        check("charset columns", row.fields.size() == 3U);
        break;
    }
    for (const auto& row : core) {
        check("engine core columns", row.fields.size() == 9U);
        break;
    }
    for (const auto& row : wrapper) {
        check("key processor columns", row.fields.size() == 11U);
        break;
    }

    // The corpus drives 2000 random sequences plus 48 curated ones.
    check("engine core case count", core.size() > 13000U);
    check("key processor case count", wrapper.size() > 13000U);

    // Hex decoding must round trip: TCVN3 'đ' is the two bytes C2 AE.
    bool foundTcvn3 = false;
    for (const auto& row : charset) {
        if (row.fields[0] == "TCVN3 (ABC)" && row.fields[1] == "U+0111") {
            foundTcvn3 = true;
            check("TCVN3 'đ' bytes", fromHex(row.fields[2]) == "\xC2\xAE");
        }
    }
    check("charset has TCVN3 'đ'", foundTcvn3);
    check("hex decode", fromHex("002041") == std::string("\x00\x20\x41", 3U));
    check("hex decode empty", fromHex("").empty());
    check("hex parse", hexValue("ff08") == 0xFF08U);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus golden smoke: charset=%zu core=%zu key_processor=%zu rows\n", charset.size(), core.size(), wrapper.size());
    return 0;
}
