/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file dictionary.cpp
 * @brief Implementation of the custom dictionary lookups.
 */
#include "dictionary.h"

#include "case_map.h"
#include "utf8.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace lotus {

    void Dictionary::assignWords(std::vector<std::u32string> words) {
        words.erase(std::ranges::remove_if(words, [](const std::u32string& word) { return word.empty(); }).begin(), words.end());
        // Go sorts and compares strings byte-wise (sort.Strings). UTF-8 keeps code point order in
        // byte order, so ordering the code points yields the same list and the same binary searches.
        std::ranges::sort(words);
        words.erase(std::ranges::unique(words).begin(), words.end());
        words_ = std::move(words);
    }

    void Dictionary::loadFromWords(const std::vector<std::string>& words) {
        std::vector<std::u32string> decoded;
        decoded.reserve(words.size());
        for (const std::string& word : words) {
            decoded.push_back(utf8::decode(word));
        }
        assignWords(std::move(decoded));
    }

    void Dictionary::loadFromText(std::string_view wholeFile) {
        std::vector<std::u32string> words;
        std::size_t                 lineStart = 0;
        while (lineStart <= wholeFile.size()) {
            const std::size_t lineEnd = wholeFile.find('\n', lineStart);
            std::string_view  line    = wholeFile.substr(lineStart, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (!line.empty()) {
                words.push_back(case_map::toLower(utf8::decode(line)));
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1;
        }
        assignWords(std::move(words));
    }

    bool Dictionary::hasWord(std::u32string_view word) const {
        if (word.empty()) {
            return false;
        }
        const auto found = std::ranges::lower_bound(words_, word, std::less{}, [](const std::u32string& stored) { return std::u32string_view(stored); });
        return found != words_.end() && std::u32string_view(*found) == word;
    }

    bool Dictionary::hasWordOrPrefix(std::u32string_view word) const {
        if (word.empty()) {
            return false;
        }
        const auto found = std::ranges::lower_bound(words_, word, std::less{}, [](const std::u32string& stored) { return std::u32string_view(stored); });
        // substr() clamps to the stored word, so a longer candidate is not a prefix (Go's
        // strings.HasPrefix).
        return found != words_.end() && std::u32string_view(*found).starts_with(word);
    }

    const std::vector<std::u32string>& Dictionary::words() const {
        return words_;
    }

} // namespace lotus
