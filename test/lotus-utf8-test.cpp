// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit test for lotus::utf8. The decoding rules must match Go's `for range string` and
// `utf8.DecodeRuneInString` (invalid sequence -> U+FFFD, one byte consumed) because the port
// of bamboo-core relies on that parity.
#include "test-utils.h"
#include "utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using lotus::utf8::decode;
    using lotus::utf8::encode;
    void expectCodePoints(const char* name, std::string_view input, const std::vector<char32_t>& expected, const std::source_location& location = std::source_location::current()) {
        const auto           actual = decode(input);
        const std::u32string actualText(actual.begin(), actual.end());
        const std::u32string expectedText(expected.begin(), expected.end());
        const bool           ok = actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
        ::check(ok, name, hexOf(expectedText), hexOf(actualText), "decode must match Go's for range string: invalid bytes become one U+FFFD per consumed byte", location);
    }

    void expectSize(const char* name, std::size_t actual, std::size_t expected, const std::source_location& location = std::source_location::current()) {
        ::check(actual == expected, name, std::to_string(expected), std::to_string(actual), "string metrics must agree with the decoded code point count", location);
    }

    void expectEncode(const char* name, std::u32string_view input, std::string_view expected, const std::source_location& location = std::source_location::current()) {
        const std::string actual = encode(input);
        ::check(actual == expected, name, std::to_string(expected.size()) + " bytes", std::to_string(actual.size()) + " bytes",
                "encode must produce the bytes Go's string(rune) produces, mapping out-of-range runes and surrogate halves to U+FFFD", location);
    }

    void expectRoundTrip(const char* name, std::string_view input, const std::source_location& location = std::source_location::current()) {
        const std::string back = encode(decode(input));
        ::check(back == input, name, "the input unchanged", "the string changed", "decode followed by encode must preserve the input byte-for-byte", location);
    }

    // The Vietnamese vowel table used by the engine (uppercase/lowercase pairs must survive a round trip).
    constexpr std::u32string_view kVowels = U"aàáảãạăằắẳẵặâầấẩẫậeèéẻẽẹêềếểễệiìíỉĩịoòóỏõọôồốổỗộơờớởỡợuùúủũụưừứửữựyỳýỷỹỵ";

} // namespace

int run_utf8() {
    // ASCII.
    expectCodePoints("ascii", "abc", {U'a', U'b', U'c'});
    expectSize("ascii length", lotus::utf8::length("abc"), 3);
    expectSize("ascii last", static_cast<std::size_t>(lotus::utf8::last("abc")), static_cast<std::size_t>(U'c'));

    // 2/3/4 byte sequences.
    expectCodePoints("2byte", "\xC3\xA9", {0xE9});
    expectSize("2byte length", lotus::utf8::length("\xC3\xA9"), 1);
    expectCodePoints("3byte", "\xE2\x98\xBA", {0x263A});
    expectCodePoints("4byte", "\xF0\x9F\x99\x82", {0x1F642});
    expectCodePoints("mixed", "a\xC3\xA9\xE2\x98\xBA\xF0\x9F\x99\x82", {U'a', 0xE9, 0x263A, 0x1F642});
    expectSize("mixed length", lotus::utf8::length("a\xC3\xA9\xE2\x98\xBA\xF0\x9F\x99\x82"), 4);
    expectSize("mixed last", static_cast<std::size_t>(lotus::utf8::last("a\xC3\xA9\xE2\x98\xBA\xF0\x9F\x99\x82")), 0x1F642);
    expectSize("empty last", static_cast<std::size_t>(lotus::utf8::last("")), 0);

    // Invalid input: one U+FFFD per consumed byte.
    expectCodePoints("overlong 2", "\xC0\x80", {0xFFFD, 0xFFFD});
    expectCodePoints("overlong 3", "\xE0\x80\x80", {0xFFFD, 0xFFFD, 0xFFFD});
    expectCodePoints("surrogate", "\xED\xA0\x80", {0xFFFD, 0xFFFD, 0xFFFD});
    expectCodePoints("beyond max", "\xF5\x80\x80\x80", {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD});
    expectCodePoints("truncated", "\xE2\x82", {0xFFFD, 0xFFFD});
    expectCodePoints("lone byte", "\xFF", {0xFFFD});
    expectCodePoints("lone continuation", "\x80", {0xFFFD});
    expectSize("invalid last", static_cast<std::size_t>(lotus::utf8::last("a\xFF")), static_cast<std::size_t>(0xFFFD));

    // encode(): out of range and surrogate halves become U+FFFD, like Go's string(rune).
    const std::u32string beyondMax(1, static_cast<char32_t>(0x110000));
    const std::u32string surrogateHalf(1, static_cast<char32_t>(0xD800));
    expectEncode("encode ascii", U"abc", "abc");
    expectEncode("encode 2byte", std::u32string_view{U"\u00E9"}, "\xC3\xA9");
    expectEncode("encode 4byte", std::u32string_view{U"\U0001F642"}, "\xF0\x9F\x99\x82");
    expectEncode("encode beyond max", beyondMax, "\xEF\xBF\xBD");
    expectEncode("encode surrogate", surrogateHalf, "\xEF\xBF\xBD");

    // Round trips, including the full Vietnamese vowel table.
    expectRoundTrip("round trip ascii", "hello world");
    expectRoundTrip("round trip vietnamese", "đường tiếng việt ăâêôơư");
    expectRoundTrip("round trip emoji", "\xF0\x9F\x99\x82\xE2\x98\xBA");
    expectRoundTrip("round trip vowels", lotus::utf8::encode(kVowels));
    expectSize("vowels count", lotus::utf8::length(lotus::utf8::encode(kVowels)), kVowels.size());

    return checkExitCode("lotus utf8: all checks passed");
}
