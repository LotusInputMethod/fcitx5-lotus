// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ports bamboo-core/encoder_test.go 1:1 and adds a byte-for-byte comparison against the golden
// charset corpus produced by the Go engine.
#include "encoder.h"
#include "golden-reader.h"
#include "utf8.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

} // namespace

int run_encoder() {
    const std::vector<GoldenRow> kCharsetGolden = readGolden(std::string(LOTUS_TEST_GOLDEN_DIR) + "/charset.tsv");

    // TestGetCharsetNames
    const std::vector<std::string>& names1 = lotus::charsetNames();
    const std::vector<std::string>& names2 = lotus::charsetNames();
    check("charset names not empty", !names1.empty());
    check("first charset is Unicode", !names1.empty() && names1[0] == lotus::kUnicodeCharset);
    check("charset names deterministic", names1 == names2);
    check("charset names count", names1.size() == 17U);

    // TestEncode
    const std::u32string input = lotus::utf8::decode("Tiếng Việt");
    check("encode Unicode", lotus::encode(lotus::kUnicodeCharset, input) == "Tiếng Việt");
    check("encode unknown charset", lotus::encode("NonExistentCharset", input) == "Tiếng Việt");
    for (const auto& name : names1) {
        if (name == lotus::kUnicodeCharset) {
            continue;
        }
        check("encode charset non-empty", !lotus::encode(name, input).empty());
    }

    // Golden corpus: every charset entry must match the bytes the Go engine produced.
    std::set<std::string> charsetsSeen;
    for (const auto& row : kCharsetGolden) {
        if (row.fields.size() != 3U) {
            check("golden row has three fields", false);
            continue;
        }
        charsetsSeen.insert(row.fields[0]);
        const std::string    codepointHex = row.fields[1].substr(2U);
        const std::u32string single(1U, static_cast<char32_t>(hexValue(codepointHex)));
        const std::string    actual = lotus::encode(row.fields[0], single);
        if (actual != fromHex(row.fields[2])) {
            std::printf("FAIL golden %s U+%s: got %zu bytes, expected %zu\n", row.fields[0].c_str(), row.fields[1].c_str(), actual.size(), fromHex(row.fields[2]).size());
            ++failures;
        }
    }
    check("golden covers every charset", charsetsSeen.size() == 17U);
    check("golden has entries", kCharsetGolden.size() > 1000U);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus encoder: all checks passed (%zu golden rows, %zu charsets)\n", kCharsetGolden.size(), charsetsSeen.size());
    return 0;
}
