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
#include <source_location>
#include <string>
#include <string_view>

/** @brief Strips directories from a path. */
inline std::string_view baseNameOf(std::string_view path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

/**
 * @brief Prints a failed expectation as Location/Step/Expected/Actual/Meaning on stderr.
 * @param step Test step that failed.
 * @param expected Expected value.
 * @param actual Observed value.
 * @param meaning Why the expectation matters.
 * @param location Call site of the failed expectation, defaults to the caller.
 */
inline void reportFailure(std::string_view step, std::string_view expected, std::string_view actual, std::string_view meaning,
                          const std::source_location& location = std::source_location::current()) {
    const std::string_view file = baseNameOf(location.file_name());
    std::fprintf(stderr, "%.*s:%u: FAILED\n", static_cast<int>(file.size()), file.data(), static_cast<unsigned>(location.line()));
    std::fprintf(stderr, "    Step: %.*s\n", static_cast<int>(step.size()), step.data());
    std::fprintf(stderr, "    Expected: %.*s\n", static_cast<int>(expected.size()), expected.data());
    std::fprintf(stderr, "    Actual: %.*s\n", static_cast<int>(actual.size()), actual.data());
    std::fprintf(stderr, "    Meaning: %.*s\n", static_cast<int>(meaning.size()), meaning.data());
}

/**
 * @brief Renders a code point list as "U+XXXX U+YYYY", used to report mismatching expectations.
 * @param text Code points to render.
 * @param empty Placeholder returned when text is empty (default: no placeholder).
 * @return Space separated hexadecimal list, or the placeholder.
 */
inline std::string hexOf(std::u32string_view text, std::string_view empty = "") {
    if (text.empty())
        return std::string(empty);
    std::string          out;
    std::array<char, 16> buffer{};
    for (const char32_t codePoint : text) {
        std::snprintf(buffer.data(), buffer.size(), "%sU+%04X", out.empty() ? "" : " ", static_cast<unsigned int>(codePoint));
        out += buffer.data();
    }
    return out;
}
