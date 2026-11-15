// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure logic checks for the diff and for the backspace unit behind the fake-backspace replacement,
// exercised with the decomposed output of "Unicode tổ hợp": the base keeps its diacritic and the tone
// becomes its own code point, so a word can hold more code points than the characters a client
// deletes. No input context, no socket, no injected key.
#include "lotus-utils.h"

#include <array>
#include <cstdio>
#include <string>

namespace {

    // "tô", "tổ", "tỗ" as the Unicode tổ hợp charset emits them.
    constexpr std::u32string_view kTo      = U"t\u00F4";
    constexpr std::u32string_view kToHook  = U"t\u00F4\u0309";
    constexpr std::u32string_view kToTilde = U"t\u00F4\u0303";
    constexpr std::u32string_view kToHop   = U"t\u00F4\u0309 h\u01A1\u0323p"; // "tổ hợp"

    int                           failures = 0;

    std::string                   codePoints(std::u32string_view text) {
        std::string          out;
        std::array<char, 16> buffer = {};
        for (const char32_t codePoint : text) {
            std::snprintf(buffer.data(), buffer.size(), "U+%04X ", static_cast<unsigned int>(codePoint));
            out += buffer.data();
        }
        return out.empty() ? "<empty>" : out;
    }

    void checkText(const char* name, std::u32string_view actual, std::u32string_view expected) {
        if (actual == expected) {
            return;
        }
        std::printf("FAIL %s: got [%s] expected [%s]\n", name, codePoints(actual).c_str(), codePoints(expected).c_str());
        ++failures;
    }

    void checkCount(const char* name, size_t actual, size_t expected) {
        if (actual == expected) {
            return;
        }
        std::printf("FAIL %s: got [%zu] expected [%zu]\n", name, actual, expected);
        ++failures;
    }

    void checkMark(const char* name, char32_t codePoint, bool expected) {
        if (isCombiningMark(codePoint) == expected) {
            return;
        }
        std::printf("FAIL %s: got [%d] expected [%d] for U+%04X\n", name, static_cast<int>(isCombiningMark(codePoint)), static_cast<int>(expected),
                    static_cast<unsigned int>(codePoint));
        ++failures;
    }

    // A combining mark on its own is not a character: the client deletes it together with the base it
    // attaches to, so the deleted part must never start with one.
    void checkStartsAtClusterBoundary(const char* name, const DiffPart& diff) {
        if (!diff.deletedPart.empty() && isCombiningMark(diff.deletedPart.front())) {
            std::printf("FAIL %s: deleted part starts with a combining mark [%s]\n", name, codePoints(diff.deletedPart).c_str());
            ++failures;
        }
    }

} // namespace

int main() {
    // Appending a tone to an existing base needs no deletion: the mark attaches to the base the
    // client already holds.
    const DiffPart appendTone = compareAndSplitU32(kTo, kToHook);
    checkText("append tone: deleted part", appendTone.deletedPart, U"");
    checkText("append tone: added part", appendTone.addedPart, U"\u0309");
    checkStartsAtClusterBoundary("append tone", appendTone);

    // Changing the tone must replace the whole cluster, never just its mark.
    const DiffPart changeTone = compareAndSplitU32(kToHook, kToTilde);
    checkText("change tone: deleted part", changeTone.deletedPart, U"\u00F4\u0309");
    checkText("change tone: added part", changeTone.addedPart, U"\u00F4\u0303");
    checkStartsAtClusterBoundary("change tone", changeTone);

    // A replacement that drops the tone also replaces the whole cluster.
    const DiffPart dropTone = compareAndSplitU32(kToHook, U"ta");
    checkText("drop tone: deleted part", dropTone.deletedPart, U"\u00F4\u0309");
    checkText("drop tone: added part", dropTone.addedPart, U"a");
    checkStartsAtClusterBoundary("drop tone", dropTone);

    // First tone on a word without one is still a pure append.
    const DiffPart firstTone = compareAndSplitU32(U"t", kToHook);
    checkText("first tone: deleted part", firstTone.deletedPart, U"");
    checkText("first tone: added part", firstTone.addedPart, U"\u00F4\u0309");

    // No combining marks: the split stays at the first differing code point.
    const DiffPart plain = compareAndSplitU32(U"to", U"ta");
    checkText("plain: deleted part", plain.deletedPart, U"o");
    checkText("plain: added part", plain.addedPart, U"a");

    // Identical texts produce an empty diff.
    const DiffPart same = compareAndSplitU32(kToHop, kToHop);
    checkText("identical: deleted part", same.deletedPart, U"");
    checkText("identical: added part", same.addedPart, U"");

    // The burst count is a cluster count, not a code point count.
    checkCount("clusters of empty", graphemeClusterCount(U""), 0U);
    checkCount("clusters of [to]", graphemeClusterCount(U"to"), 2U);
    checkCount("clusters of [tô]", graphemeClusterCount(kTo), 2U);
    checkCount("clusters of [tổ]", graphemeClusterCount(kToHook), 2U);
    checkCount("clusters of [tổ hợp]", graphemeClusterCount(kToHop), 6U);
    checkCount("clusters of a lone mark", graphemeClusterCount(U"\u0309"), 1U);

    checkMark("U+0309 is a mark", U'\u0309', true);
    checkMark("U+0300 is a mark", U'\u0300', true);
    checkMark("U+031B (horn) is a mark", U'\u031B', true);
    checkMark("U+0323 (dot below) is a mark", U'\u0323', true);
    checkMark("U+00F4 (ô) is not a mark", U'\u00F4', false);
    checkMark("a is not a mark", U'a', false);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("unicode cluster: all checks passed\n");
    return 0;
}
