// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit regression test for eraseLastUtf8Codepoint().
//
// The backspace branch in LotusState::handleEmojiMode must erase the
// last *codepoint* (1-4 UTF-8 bytes) so the preedit buffer stays valid
// UTF-8. The helper that does the work is eraseLastUtf8Codepoint() in
// lotus-utils; this test calls that same helper, so the test cannot
// drift from production behavior.
//
// Each case asserts both:
//   - the buffer is truncated to the expected substring, and
//   - the result is well-formed UTF-8 (using fcitx::utf8::validate).
//
#include "lotus-utils.h"

#include <cstdio>
#include <string>
#include <vector>

#include <fcitx-utils/utf8.h>

namespace {

    struct EmojiBackspaceCase {
        const char* name;
        std::string input;
        std::string expected;
    };

    void dumpBytes(const char* label, const std::string& s) {
        std::printf("    %s =", label);
        for (unsigned char c : s) {
            std::printf(" \\x%02X", c);
        }
        std::printf("\n");
    }

} // namespace

int main() {
    const std::vector<EmojiBackspaceCase> cases = {
        // ASCII: still works, sanity check.
        {"ascii_abc", "abc", "ab"},
        // 2-byte: U+00E9 LATIN SMALL LETTER E WITH ACUTE
        {"ascii_then_2byte", std::string("a\xC3\xA9"), "a"},
        // 3-byte: U+263A WHITE SMILING FACE
        {"ascii_then_3byte", std::string("a\xE2\x98\xBA"), "a"},
        // 4-byte: U+1F642 SLIGHTLY SMILING FACE
        {"emoji_only_4byte", std::string("\xF0\x9F\x99\x82"), ""},
        // 4-byte after ASCII
        {"ascii_then_4byte", std::string("a\xF0\x9F\x99\x82"), "a"},
    };

    int failures = 0;
    for (const auto& tc : cases) {
        std::string buffer = tc.input;
        eraseLastUtf8Codepoint(buffer);
        const bool matches = (buffer == tc.expected);
        const bool valid   = buffer.empty() || fcitx::utf8::validate(buffer);
        const bool ok      = matches && valid;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", tc.name);
        if (!ok) {
            dumpBytes("input   ", tc.input);
            dumpBytes("expected", tc.expected);
            dumpBytes("got     ", buffer);
            std::printf("    matches=%d validUtf8=%d\n", matches, valid);
            ++failures;
        }
    }

    const int total = static_cast<int>(cases.size());
    std::printf("\n%d/%d emoji-backspace tests passed\n", total - failures, total);
    return failures == 0 ? 0 : 1;
}
