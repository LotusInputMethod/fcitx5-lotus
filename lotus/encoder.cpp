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

#include "utf8.h"

#include <cstddef>

namespace lotus {

    std::string encode(Charset charset, std::u32string_view text) {
        if (charset == Charset::Unicode) {
            return utf8::encode(text);
        }
        const auto& table = data::charsetDefinitions().at(static_cast<std::size_t>(charset));
        std::string output;
        for (char32_t codePoint : text) {
            const auto entry = table.find(codePoint);
            if (entry != table.end()) {
                output.append(entry->second);
            } else {
                utf8::appendEncoded(output, codePoint);
            }
        }
        return output;
    }

} // namespace lotus
