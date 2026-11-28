// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
/**
 * @file test-utils.h
 * @brie Failure reporting and code point formatting.
 */
#pragma once

#include <array>
#include <cstdio>
#include <source_location>
#include <string>
#include <string_view>

/** @brief Strips directories from a path. */
[[nodiscard]] inline std::string_view baseNameOf(std::string_view path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

/** @brief Failed checks counted by reportFailure in the current process. */
inline int g_failedChecks = 0;

/**
 * @brief Counts a failed expectation and prints it as Location/Step/Expected/Actual/Meaning on stderr.
 * @param step Test step that failed.
 * @param expected Expected value.
 * @param actual Observed value.
 * @param meaning Why the expectation matters.
 * @param location Call site of the failed expectation, defaults to the caller.
 */
inline void reportFailure(std::string_view step, std::string_view expected, std::string_view actual, std::string_view meaning,
                          const std::source_location& location = std::source_location::current()) {
    ++g_failedChecks;
    const std::string_view file = baseNameOf(location.file_name());
    std::fprintf(stderr, "%.*s:%u: FAILED\n", static_cast<int>(file.size()), file.data(), static_cast<unsigned>(location.line()));
    std::fprintf(stderr, "    Step: %.*s\n", static_cast<int>(step.size()), step.data());
    std::fprintf(stderr, "    Expected: %.*s\n", static_cast<int>(expected.size()), expected.data());
    std::fprintf(stderr, "    Actual: %.*s\n", static_cast<int>(actual.size()), actual.data());
    std::fprintf(stderr, "    Meaning: %.*s\n", static_cast<int>(meaning.size()), meaning.data());
}

/**
 * @brief Prints the run outcome and returns the process exit code.
 * @param passed Message printed when no check failed, without trailing newline.
 * @return 0 when no check failed, 1 otherwise.
 */
[[nodiscard]] inline int checkExitCode(std::string_view passed = "") {
    if (g_failedChecks != 0) {
        std::printf("%d failure(s)\n", g_failedChecks);
        return 1;
    }
    if (!passed.empty())
        std::printf("%.*s\n", static_cast<int>(passed.size()), passed.data());
    return 0;
}

/**
 * @brief Renders a code point list as "U+XXXX U+YYYY", used to report mismatching expectations.
 * @param text Code points to render.
 * @param empty Placeholder returned when text is empty (default: no placeholder).
 * @return Space separated hexadecimal list, or the placeholder.
 */
[[nodiscard]] inline std::string hexOf(std::u32string_view text, std::string_view empty = "") {
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

/** @brief Checks a bare boolean expectation. */
inline bool check(std::string_view name, bool condition, std::string_view meaning = "", const std::source_location& location = std::source_location::current()) {
    if (!condition)
        reportFailure(name, "true", "false", meaning, location);
    return condition;
}

/** @brief Checks an expectation with expected and actual values, in reportFailure()'s printing order. */
inline bool check(bool ok, std::string_view step, std::string_view expected, std::string_view actual, std::string_view meaning,
                  const std::source_location& location = std::source_location::current()) {
    if (!ok)
        reportFailure(step, expected, actual, meaning, location);
    return ok;
}

/** @brief Checks that two code point views are equal, reporting both sides as hex (actual first). */
inline void checkU32(std::string_view name, std::u32string_view actual, std::u32string_view expected, std::string_view meaning = "",
                     const std::source_location& location = std::source_location::current()) {
    if (actual != expected)
        reportFailure(name, hexOf(expected, "<empty>"), hexOf(actual, "<empty>"), meaning, location);
}

/** @brief Renders a bool as "true" or "false". */
[[nodiscard]] inline const char* boolText(bool value) {
    return value ? "true" : "false";
}

/** @brief Counts golden mismatches the print cap kept silent as failed checks. */
inline void addSilentFailures(int mismatches, int shown) {
    g_failedChecks += mismatches - shown;
}
