/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-mode-menu.h
 * @brief Mode Order resolution and the mode menu shortcuts.
 */

#ifndef _FCITX5_LOTUS_MODE_MENU_H_
#define _FCITX5_LOTUS_MODE_MENU_H_

#include "lotus-config.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <fcitx-utils/keysym.h>

namespace fcitx {

    /**
     * @brief One mode name accepted in Mode Order.
     */
    struct ModeNameEntry {
        std::string_view         name; ///< Name used in Mode Order.
        std::optional<LotusMode> mode; ///< Mode the name resolves to, or nothing for the synthetic entry.
    };

    /**
     * @brief Mode Order names, in the fallback order used when a name is not listed.
     */
    inline constexpr auto kModeNames = std::to_array<ModeNameEntry>({
        {.name = "Smooth", .mode = LotusMode::Smooth},
        {.name = "SurroundingText", .mode = LotusMode::SurroundingText},
        {.name = "Preedit", .mode = LotusMode::Preedit},
        {.name = "Emoji", .mode = LotusMode::Emoji},
        {.name = "Off", .mode = LotusMode::Off},
        {.name = "SuperSmooth", .mode = LotusMode::SuperSmooth},
        {.name = "Default", .mode = std::nullopt},
    });

    /**
     * @brief One mode resolved from Mode Order, in menu order.
     */
    struct ModeOrderEntry {
        LotusMode mode;      ///< Mode of the entry.
        KeySym    key;       ///< Shortcut key that selects the entry.
        bool      visible;   ///< Whether the entry is shown.
        bool      isDefault; ///< Whether the entry is the synthetic Default Typing one.
    };

    /**
     * @brief Whether a mode is shown in the mode menu and in the cycle list.
     * @param config Current configuration.
     * @param mode Mode to query.
     * @return True when the mode is visible.
     */
    bool modeVisible(const lotusConfig& config, LotusMode mode);

    /**
     * @brief Shortcut key that selects a mode from the mode menu.
     * @param config Current configuration.
     * @param mode Mode to query.
     * @return The shortcut key, or FcitxKey_None when the mode has none.
     */
    KeySym modeShortcut(const lotusConfig& config, LotusMode mode);

    /**
     * @brief Label of a menu entry, taken from the translated enum names.
     * @param entry Menu entry.
     * @return Translated label of the entry.
     */
    std::string modeLabel(const ModeOrderEntry& entry);

    /**
     * @brief Resolves the modes in Mode Order order, then the names missing from it.
     * @param config Current configuration.
     * @return The menu entries in menu order.
     */
    std::vector<ModeOrderEntry> modeOrderEntries(const lotusConfig& config);

    /**
     * @brief Parses a shortcut string from the configuration into its key symbol.
     * @param shortcut Shortcut text, as stored in the configuration.
     * @return Key symbol of the shortcut.
     */
    KeySym shortcutKey(const std::string& shortcut);

    /**
     * @brief Whether a key is reserved by the mode menu.
     * @param sym Key symbol to test.
     * @param config Current configuration.
     * @return True when the key is reserved.
     */
    bool isAppModeMenuReservedKey(KeySym sym, const lotusConfig& config);

    /**
     * @brief Key symbol that triggers the "type the hotkey character" menu entry.
     * @param hotkeySym Key symbol of the mode menu hotkey.
     * @param config Current configuration.
     * @return Key symbol used to type the hotkey character.
     */
    KeySym typeKeyForModeMenuHotkey(KeySym hotkeySym, const lotusConfig& config);

} // namespace fcitx

#endif // _FCITX5_LOTUS_MODE_MENU_H_
