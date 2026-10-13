/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file encoder.cpp
 * @brief Implementation of the output charset encoder.
 */
#include "encoder.h"

#include "data/charset_data.h"
#include "utf8.h"

#include <algorithm>

namespace lotus {

    std::vector<std::string> buildCharsetNames() {
        std::vector<std::string> others;
        others.reserve(data::charsetDefinitions().size());
        for (const auto& table : data::charsetDefinitions()) {
            others.emplace_back(table.first);
        }
        std::ranges::sort(others);

        std::vector<std::string> names;
        names.reserve(others.size() + 1U);
        names.emplace_back(kUnicodeCharset);
        names.insert(names.end(), others.begin(), others.end());
        return names;
    }

    const std::vector<std::string>& charsetNames() {
        static const std::vector<std::string> kNames = buildCharsetNames();
        return kNames;
    }

    std::string encode(std::string_view charsetName, std::u32string_view text) {
        if (charsetName == kUnicodeCharset) {
            return utf8::encode(text);
        }
        const auto table = data::charsetDefinitions().find(charsetName);
        if (table == data::charsetDefinitions().end()) {
            return utf8::encode(text);
        }
        std::string output;
        for (char32_t codePoint : text) {
            const auto entry = table->second.find(codePoint);
            if (entry != table->second.end()) {
                output.append(entry->second);
            } else {
                utf8::appendEncoded(output, codePoint);
            }
        }
        return output;
    }

} // namespace lotus
