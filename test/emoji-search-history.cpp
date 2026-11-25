// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file emoji-search-history.cpp
 * @brief Production-backed ranking tests for EmojiLoader::search.
 *
 * Calls the real EmojiLoader::search (no algorithm copies) and checks the
 * tier order exact > prefix > word-boundary > fuzzy, case-insensitivity,
 * the Top-K output cap and deterministic tie-breaking.
 */

#include "emoji.h"
#include "test-utils.h"

#include <algorithm>
#include <filesystem>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace {
    void check(bool ok, const std::string& step, const std::string& expected, const std::string& actual, std::string_view meaning,
               const std::source_location& location = std::source_location::current()) {
        if (ok) {
            return;
        }
        reportFailure(step, expected, actual, meaning, location);
    }

    std::string triggers(const std::vector<EmojiEntry>& entries) {
        std::string out;
        out.reserve(entries.size() * 12);
        for (const auto& entry : entries) {
            out += '[';
            out += entry.trigger;
            out += ']';
        }
        return {out};
    }

    std::vector<EmojiEntry> tierEntries() {
        return {
            {.trigger = "cat", .output = "C"},         {.trigger = "cat_face", .output = "F"}, {.trigger = "black_cat", .output = "B"},
            {.trigger = "educational", .output = "E"}, {.trigger = "dog", .output = "D"},
        };
    }

    std::size_t indexOf(const std::vector<EmojiEntry>& results, const std::string& trigger) {
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (results[i].trigger == trigger) {
                return i;
            }
        }
        return results.size();
    }

    bool contains(const std::vector<EmojiEntry>& results, const std::string& trigger) {
        return std::ranges::any_of(results, [&](const EmojiEntry& entry) { return entry.trigger == trigger; });
    }

} // namespace

int main() {
    // Seams link: point history at a missing file; history reloads empty.
    {
        EmojiLoader     loader(nullptr);
        const auto      missing = std::filesystem::temp_directory_path() / "lotus-emoji-search-history-missing.conf";
        std::error_code ec;
        std::filesystem::remove(missing, ec);
        loader.setHistoryPathForTest(missing.string());
        check(loader.history().empty(), "missing history", "empty", std::to_string(loader.history().size()),
              "a missing history file must load as empty instead of failing the search");
    }

    // Tier order exact > prefix > boundary > fuzzy, plus case-insensitive exact.
    {
        EmojiLoader loader(nullptr);
        loader.setEmojiListForTest(tierEntries());

        const auto results = loader.search("cat");
        check(results.size() == 4, "tier size", "4", triggers(results), "the exact, prefix, boundary and fuzzy tiers together return the four matching entries");
        check(indexOf(results, "cat") < indexOf(results, "cat_face"), "exact>prefix", "cat first", triggers(results), "an exact trigger must rank above a prefix match");
        check(indexOf(results, "cat_face") < indexOf(results, "black_cat"), "prefix>boundary", "cat_face before black_cat", triggers(results),
              "a prefix match must rank above a word-boundary match");
        check(indexOf(results, "black_cat") < indexOf(results, "educational"), "boundary>fuzzy", "black_cat before educational", triggers(results),
              "a word-boundary match must rank above a fuzzy match");
        check(!contains(results, "dog"), "non-match excluded", "no dog", triggers(results), "entries that do not match the query must stay out of the results");

        const auto upper = loader.search("CAT");
        check(!upper.empty() && upper[0].trigger == "cat", "CAT exact ci", "cat", triggers(upper), "exact matching must be case-insensitive");

        check(loader.search("xyz").empty(), "non-subsequence", "0", triggers(loader.search("xyz")), "a query no entry matches must return no results");
        check(loader.search("").empty(), "empty", "0", "non-empty", "an empty query must return no results");
    }

    // Top-K: the best match is inserted last, so naive truncation (first 100)
    // would drop it; selection must keep the best 100 instead.
    {
        EmojiLoader             loader(nullptr);
        std::vector<EmojiEntry> entries;
        entries.reserve(101);
        for (int i = 0; i < 100; ++i) {
            entries.push_back({.trigger = "rank_" + std::to_string(i), .output = "E"});
        }
        entries.push_back({.trigger = "rank", .output = "BEST"});
        loader.setEmojiListForTest(std::move(entries));
        const auto results = loader.search("rank");
        check(results.size() == EmojiLoader::kMaxEmojiSearchResults, "top-k size", "100", std::to_string(results.size()),
              "search must cap the result list at kMaxEmojiSearchResults");
        check(!results.empty() && results[0].trigger == "rank", "best match first", "rank", triggers(results), "the best match must survive the cap and lead the results");
        check(!contains(results, "rank_99"), "worst dropped", "rank_99 absent", triggers(results), "truncation must drop the worst match instead of the best one");
    }

    // Duplicate triggers keep insertion order (deterministic tie-break).
    {
        EmojiLoader loader(nullptr);
        loader.setEmojiListForTest({
            {.trigger = "dup", .output = "A"},
            {.trigger = "dup", .output = "B"},
        });
        const auto results = loader.search("dup");
        check(results.size() == 2, "dup size", "2", triggers(results), "duplicate triggers must both be returned");
        check(results.size() == 2 && results[0].output == "A" && results[1].output == "B", "dup order", "[A][B]",
              results.size() == 2 ? "[" + results[0].output + "][" + results[1].output + "]" : triggers(results),
              "duplicate triggers must keep insertion order so tie-breaking is deterministic");
    }

    return checkExitCode("emoji-search-history: all checks passed");
}
