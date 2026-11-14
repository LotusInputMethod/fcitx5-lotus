// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
/**
 * @file test-util.h
 * @brief Failure reporting and code point formatting.
 */
#pragma once

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

/**
 * @brief Prints a failed expectation as Step/Expected/Actual/Meaning on stderr.
 * @param step Test step that failed.
 * @param expected Expected value.
 * @param actual Observed value.
 * @param meaning Why the expectation matters.
 */
inline void reportFailure(std::string_view step, std::string_view expected, std::string_view actual, std::string_view meaning) {
    std::fprintf(stderr, "Step: %.*s\n", static_cast<int>(step.size()), step.data());
    std::fprintf(stderr, "Expected: %.*s\n", static_cast<int>(expected.size()), expected.data());
    std::fprintf(stderr, "Actual: %.*s\n", static_cast<int>(actual.size()), actual.data());
    std::fprintf(stderr, "Meaning: %.*s\n", static_cast<int>(meaning.size()), meaning.data());
}

/**
 * @brief Renders a code point list as "U+XXXX U+YYYY", used to report mismatching expectations.
 * @param text Code points to render.
 * @return Space separated hexadecimal list.
 */
inline std::string hexOf(std::u32string_view text) {
    std::string          out;
    std::array<char, 16> buffer{};
    for (const char32_t codePoint : text) {
        std::snprintf(buffer.data(), buffer.size(), "%sU+%04X", out.empty() ? "" : " ", static_cast<unsigned int>(codePoint));
        out += buffer.data();
    }
    return out;
}
