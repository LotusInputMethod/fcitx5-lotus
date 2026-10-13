/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file macro_table.cpp
 * @brief Implementation of the text macro expansion table.
 */
#include "macro_table.h"

#include "case_map.h"
#include "utf8.h"

namespace lotus {

    bool MacroTable::empty() const {
        return table_.empty();
    }

    std::optional<std::u32string_view> MacroTable::get(std::u32string_view key) const {
        if (key.empty() || empty()) {
            return std::nullopt;
        }
        std::u32string lowered(key);
        for (char32_t& codePoint : lowered) {
            codePoint = case_map::toLower(codePoint);
        }
        const auto found = table_.find(lowered);
        if (found == table_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    void MacroTable::set(std::string_view keyUtf8, std::string_view valueUtf8) {
        table_.insert_or_assign(utf8::decode(keyUtf8), utf8::decode(valueUtf8));
    }

} // namespace lotus
