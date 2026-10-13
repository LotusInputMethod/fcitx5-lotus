/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file macro_table.h
 * @brief Text macro expansion table, ported 1:1 from bamboo/macrotable.go.
 *
 * A macro maps the text of the word being typed to its replacement ("vn" -> "Việt Nam"). Like
 * Go's MacroTable, the table stores the keys verbatim and lowercases the query before the lookup,
 * so "VN" typed by the user still matches the "vn" entry. The engine shares one table between
 * input contexts through a std::shared_ptr, so the class owns no raw pointer and stays copyable.
 */

#ifndef _LOTUS_MACRO_TABLE_H_
#define _LOTUS_MACRO_TABLE_H_

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lotus {

    /**
     * @brief Key to replacement text map driving the macro expansion, like Go's MacroTable.
     */
    class MacroTable {
      public:
        /**
         * @brief Reports whether the table holds no entry, like Go's MacroTable.Empty.
         * @return True when the table is empty. A nil Go table also reports true; on the C++ side a
         *         missing table is a null std::shared_ptr, which callers check before the lookup.
         */
        bool empty() const;

        /**
         * @brief Looks a key up case-insensitively, like Go's MacroTable.Get.
         * @param key Text to expand; an empty key never matches. The query is lowercased code point
         *            by code point, like Go lowercases the string.
         * @return The replacement text, or std::nullopt on a miss (what Go returns as ("", false)
         *         there). The view points into the table and stays valid while the table is unchanged.
         */
        [[nodiscard]] std::optional<std::u32string_view> get(std::u32string_view key) const;

        /**
         * @brief Stores a replacement text under a key, like the map literal and NewMacroTable of Go.
         * @param keyUtf8 Key, stored as given because Go keeps its configuration keys verbatim, so
         *                an entry only matches when the key is lowercase.
         * @param valueUtf8 Replacement text.
         */
        void set(std::string_view keyUtf8, std::string_view valueUtf8);

      private:
        // Go: map[string]string, keyed by code points.
        std::unordered_map<std::u32string, std::u32string> table_;
    };

} // namespace lotus

#endif // _LOTUS_MACRO_TABLE_H_
