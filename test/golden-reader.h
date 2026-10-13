// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only reader for the golden corpus written by bamboo/golden_dump_test.go.
// Regenerate the corpus with:
//     cd bamboo && LOTUS_DUMP_GOLDEN=1 go test -run TestDumpGoldenCorpus -count=1
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

/** @brief One data line of a golden TSV file, split on tabs. */
struct GoldenRow {
    std::vector<std::string> fields;
};

/** @brief Decodes a hex encoded byte string (empty input yields an empty string). */
inline std::string fromHex(std::string_view hex) {
    auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') {
            return static_cast<unsigned>(c - '0');
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<unsigned>(c - 'A') + 10U;
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<unsigned>(c - 'a') + 10U;
        }
        return 0U;
    };
    std::string out;
    out.reserve(hex.size() / 2U);
    for (std::size_t i = 0; i + 1U < hex.size(); i += 2U) {
        out.push_back(static_cast<char>((nibble(hex[i]) << 4U) | nibble(hex[i + 1U])));
    }
    return out;
}

/** @brief Reads a golden TSV file, skipping empty lines and '#' comments. */
inline std::vector<GoldenRow> readGolden(const std::string& path) {
    std::vector<GoldenRow> rows;
    std::ifstream          file(path);
    std::string            line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        GoldenRow   row;
        std::size_t start = 0;
        while (true) {
            const auto tab = line.find('\t', start);
            if (tab == std::string::npos) {
                row.fields.push_back(line.substr(start));
                break;
            }
            row.fields.push_back(line.substr(start, tab - start));
            start = tab + 1U;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

/** @brief Parses a hexadecimal number field. */
inline uint32_t hexValue(const std::string& field) {
    return static_cast<uint32_t>(std::stoul(field, nullptr, 16));
}
