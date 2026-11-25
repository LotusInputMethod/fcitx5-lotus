// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ports bamboo-core/encoder_test.go 1:1 and adds a byte-for-byte comparison against the golden
// charset corpus produced by the Go engine.
#include "encoder.h"
#include "golden-reader.h"
#include "test-utils.h"
#include "utf8.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <source_location>
#include <string>
#include <vector>

namespace {
    void check(const char* name, bool condition, const std::source_location& location = std::source_location::current()) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "the encoder must produce the same bytes as the Go engine for every charset", location);
        }
    }

} // namespace

int run_encoder() {
    const std::vector<GoldenRow>                                 kCharsetGolden = readGolden(std::string(LOTUS_TEST_GOLDEN_DIR) + "/charset.tsv");

    constexpr std::array<std::string_view, lotus::kCharsetCount> kNames = {
        "Unicode",        "BKHCM 1", "BKHCM 2", "NCR Decimal", "NCR Hex", "TCVN3 (ABC)",   "UTF-8",      "Unicode C string Decimal", "Unicode C string Hex",
        "Unicode tổ hợp", "VIQR",    "VISCII",  "VNI Windows", "VPS",     "Vietware Full", "Vietware X", "Windows 1258 codepage",
    };
    check("charset names count", kNames.size() == lotus::kCharsetCount);
    check("first charset is Unicode", kNames[0] == "Unicode");

    // TestEncode
    const std::u32string input = lotus::utf8::decode("Tiếng Việt");
    check("encode Unicode", lotus::encode(lotus::Charset::Unicode, input) == "Tiếng Việt");
    for (std::size_t i = 0; i < lotus::kCharsetCount; ++i) {
        const auto charset = static_cast<lotus::Charset>(i);
        if (charset == lotus::Charset::Unicode) {
            check("unicode table is empty", lotus::data::kCharsetTables.at(i).empty());
            continue;
        }
        check("charset table exists", !lotus::data::kCharsetTables.at(i).empty());
        check("encode charset non-empty", !lotus::encode(charset, input).empty());
    }

    // Golden corpus: every charset entry must match the bytes the Go engine produced.
    std::set<std::string> charsetsSeen;
    for (const auto& row : kCharsetGolden) {
        if (row.fields.size() != 3U) {
            check("golden row has three fields", false);
            continue;
        }
        charsetsSeen.insert(row.fields[0]);
        const auto* const name = std::ranges::find(kNames, row.fields[0]);
        if (name == kNames.end()) {
            check("golden charset is a known enum", false);
            continue;
        }
        const std::string    codepointHex = row.fields[1].substr(2U);
        const std::u32string single(1U, static_cast<char32_t>(hexValue(codepointHex)));
        const std::string    actual = lotus::encode(static_cast<lotus::Charset>(static_cast<std::size_t>(name - kNames.begin())), single);
        if (actual != fromHex(row.fields[2])) {
            reportFailure("golden " + row.fields[0] + " " + row.fields[1], std::to_string(fromHex(row.fields[2]).size()) + " bytes", std::to_string(actual.size()) + " bytes",
                          "every charset entry must match the bytes the Go engine produced");
        }
    }
    check("golden covers every charset", charsetsSeen.size() == 17U);
    check("golden has entries", kCharsetGolden.size() > 1000U);

    if (checkExitCode() != 0)
        return 1;
    std::printf("lotus encoder: all checks passed (%zu golden rows, %zu charsets)\n", kCharsetGolden.size(), charsetsSeen.size());
    return 0;
}
