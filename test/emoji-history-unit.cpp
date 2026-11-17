// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression tests for EmojiLoader history: newest-9 cap, dedup,
// malformed-line filtering, record ordering, save round-trip and
// failed-write preservation.
#include "emoji.h"
#include "test-utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

    int  failures = 0;

    void check(bool ok, const std::string& step, const std::string& expected, const std::string& actual, std::string_view meaning) {
        if (ok) {
            return;
        }
        ++failures;
        reportFailure(step, expected, actual, meaning);
    }

    std::string outputsStr(const std::vector<EmojiEntry>& entries) {
        std::string out = "[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            out += entries[i].output;
        }
        return out + "]";
    }

    void writeText(const std::filesystem::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }

    std::string readText(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        return {(std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>())};
    }

    bool hasTmpLitter(const std::filesystem::path& dir) {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator{}; ++it) {
            if (it->path().filename().string().find(".tmp") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    EmojiLoader makeLoader(const std::filesystem::path& path) {
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        return loader;
    }

} // namespace

int main() {
    const auto      root = std::filesystem::temp_directory_path() / ("fcitx5-lotus-emoji-history-unit-" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    // Load keeps the newest 9 of 12; file order is newest-first.
    {
        const auto  path = root / "cap.conf";
        std::string text;
        text.reserve(static_cast<size_t>(12) * 16);
        for (int i = 0; i < 12; ++i) {
            text += "t" + std::to_string(i) + "=e" + std::to_string(i) + "\n";
        }
        writeText(path, text);
        auto loader = makeLoader(path);
        check(loader.history().size() == 9, "load cap", "9", std::to_string(loader.history().size()), "history must keep at most the newest 9 entries");
        check(!loader.history().empty() && loader.history().front().output == "e0" && loader.history().back().output == "e8", "newest first", "[e0..e8]",
              outputsStr(loader.history()), "the file stores newest-first, so the loaded history must preserve that order");
    }

    // Duplicates collapse to one entry; the newest occurrence wins.
    {
        const auto path = root / "dupe.conf";
        writeText(path, "t1=dupe\nt0=keep0\nt1again=dupe\nt2=keep2\n");
        auto loader = makeLoader(path);
        check(outputsStr(loader.history()) == "[dupe,keep0,keep2]", "duplicates collapse", "[dupe,keep0,keep2]", outputsStr(loader.history()),
              "the same emoji recorded repeatedly must collapse into a single history entry");
        check(!loader.history().empty() && loader.history()[0].trigger == "t1", "newest trigger wins", "t1", loader.history().empty() ? "empty" : loader.history()[0].trigger,
              "the trigger of the newest occurrence must win so the user can retype it");
    }

    // Malformed lines are ignored; '=' splits on the first occurrence.
    {
        const auto path = root / "malformed.conf";
        writeText(path, "noequal\n\nt-empty=\nt=ok\nt2=a=b\n");
        auto loader = makeLoader(path);
        check(outputsStr(loader.history()) == "[ok,a=b]", "malformed ignored", "[ok,a=b]", outputsStr(loader.history()),
              "malformed lines must be skipped without discarding the valid entries around them");
    }

    // recordHistory moves entries to the front and keeps the cap.
    {
        const auto  path = root / "record.conf";
        std::string text;
        text.reserve(static_cast<size_t>(9) * 16);
        for (int i = 0; i < 9; ++i) {
            text += "t" + std::to_string(i) + "=e" + std::to_string(i) + "\n";
        }
        writeText(path, text);
        auto loader = makeLoader(path);
        loader.recordHistory(EmojiEntry{.trigger = "t9", .output = "e9"});
        check(outputsStr(loader.history()) == "[e9,e0,e1,e2,e3,e4,e5,e6,e7]", "record prepends and caps", "[e9,e0,e1,e2,e3,e4,e5,e6,e7]", outputsStr(loader.history()),
              "recording must move the emoji to the front and evict the oldest entry beyond the cap");
        loader.recordHistory(EmojiEntry{.trigger = "t5x", .output = "e5"});
        check(loader.history().size() == 9 && loader.history().front().output == "e5", "record reorders", "9 entries, e5 first", outputsStr(loader.history()),
              "recording an already stored emoji must reorder it instead of duplicating it");
    }

    // Save then reload preserves order, including UTF-8 emoji.
    {
        const auto path   = root / "roundtrip.conf";
        auto       loader = makeLoader(path);
        loader.recordHistory(EmojiEntry{.trigger = "sushi", .output = "🍣"});
        loader.recordHistory(EmojiEntry{.trigger = "cat", .output = "🐱"});
        auto reloaded = makeLoader(path);
        check(outputsStr(reloaded.history()) == outputsStr(loader.history()), "save round-trip", outputsStr(loader.history()), outputsStr(reloaded.history()),
              "the saved history must reload identically, including UTF-8 emoji");
    }

    // A failed write must not destroy the previous valid history.
    {
        if (::geteuid() == 0) {
            std::cout << "skip failed-write check: running as root\n";
        } else {
            const auto dir = root / "readonly";
            std::filesystem::create_directories(dir, ec);
            const auto path = dir / "history.conf";
            writeText(path, "t0=e0\nt1=e1\n");
            const std::string before = readText(path);
            auto              loader = makeLoader(path);
            std::filesystem::permissions(dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec, ec);
            check(!ec, "read-only setup", "ec clear", ec ? ec.message() : "clear", "the failed-write check needs a read-only directory to simulate an unwritable history");
            if (!ec) {
                loader.recordHistory(EmojiEntry{.trigger = "t2", .output = "e2"});
                check(readText(path) == before, "failed write preserves", "file unchanged", readText(path) == before ? "unchanged" : "modified",
                      "a failed save must not destroy the previous valid history");
            }
            std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
        }
    }

    check(!hasTmpLitter(root), "no tmp litter", "none", hasTmpLitter(root) ? "found" : "none", "the atomic save must not leave temporary files behind");

    std::filesystem::remove_all(root, ec);
    if (failures == 0) {
        std::cout << "emoji history unit: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
