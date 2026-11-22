/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-mode-menu.cpp
 * @brief Mode Order resolution and the mode menu shortcuts.
 */

#include "lotus-mode-menu.h"

#include <algorithm>
#include <array>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/stringutils.h>

namespace fcitx {

    KeySym shortcutKey(const std::string& shortcut) {
        return Key(shortcut).sym();
    }

    bool modeVisible(const lotusConfig& config, LotusMode mode) {
        switch (mode) {
            case LotusMode::Smooth: return *config.showModeSmooth;
            case LotusMode::SuperSmooth: return *config.showModeSuperSmooth;
            case LotusMode::SurroundingText: return *config.showModeSurroundingText;
            case LotusMode::Preedit: return *config.showModePreedit;
            case LotusMode::Emoji: return *config.showModeEmoji;
            case LotusMode::Off: return *config.showModeOff;
        }
        return false;
    }

    KeySym modeShortcut(const lotusConfig& config, LotusMode mode) {
        switch (mode) {
            case LotusMode::Smooth: return shortcutKey(*config.shortcutSmooth);
            case LotusMode::SuperSmooth: return shortcutKey(*config.shortcutSuperSmooth);
            case LotusMode::SurroundingText: return shortcutKey(*config.shortcutSurroundingText);
            case LotusMode::Preedit: return shortcutKey(*config.shortcutPreedit);
            case LotusMode::Emoji: return shortcutKey(*config.shortcutEmoji);
            case LotusMode::Off: return shortcutKey(*config.shortcutOff);
        }
        return FcitxKey_None;
    }

    std::string modeLabel(const ModeOrderEntry& entry) {
        return entry.isDefault ? _("Default Typing") : LotusModeI18NAnnotation::toString(entry.mode);
    }

    std::vector<ModeOrderEntry> modeOrderEntries(const lotusConfig& config) {
        const auto order = stringutils::split(*config.modeOrder, ",");

        auto       resolve = [&config](const ModeNameEntry& nameEntry) {
            if (nameEntry.mode.has_value()) {
                return ModeOrderEntry{.mode = *nameEntry.mode, .key = modeShortcut(config, *nameEntry.mode), .visible = modeVisible(config, *nameEntry.mode), .isDefault = false};
            }
            return ModeOrderEntry{.mode = config.mode.value(), .key = shortcutKey(*config.shortcutDefault), .visible = *config.showModeDefault, .isDefault = true};
        };

        std::vector<ModeOrderEntry> entries;
        for (const auto& name : order) {
            const auto* const it = std::ranges::find_if(kModeNames, [&name](const ModeNameEntry& entry) { return entry.name == name; });
            if (it != kModeNames.end()) {
                entries.push_back(resolve(*it));
            }
        }
        for (const auto& nameEntry : kModeNames) {
            const bool listed = std::ranges::any_of(order, [&nameEntry](const std::string& name) { return nameEntry.name == name; });
            if (!listed) {
                entries.push_back(resolve(nameEntry));
            }
        }
        return entries;
    }

    bool isAppModeMenuReservedKey(KeySym sym, const lotusConfig& config) {
        std::array<KeySym, kModeNames.size()> modeShortcuts{};
        std::ranges::transform(kModeNames, modeShortcuts.begin(), [&config](const ModeNameEntry& nameEntry) {
            if (nameEntry.mode.has_value()) {
                return modeShortcut(config, *nameEntry.mode);
            }
            return shortcutKey(*config.shortcutDefault);
        });
        if (std::ranges::find(modeShortcuts, sym) != modeShortcuts.end()) {
            return true;
        }

        static const auto reservedMenuKeys = std::to_array<KeySym>({
            FcitxKey_Escape,
            FcitxKey_Tab,
            FcitxKey_ISO_Left_Tab,
            FcitxKey_Return,
            FcitxKey_space,
            FcitxKey_Up,
            FcitxKey_Down,
        });
        return std::ranges::find(reservedMenuKeys, sym) != reservedMenuKeys.end();
    }

    KeySym typeKeyForModeMenuHotkey(KeySym hotkeySym, const lotusConfig& config) {
        return isAppModeMenuReservedKey(hotkeySym, config) ? FcitxKey_f : hotkeySym;
    }

} // namespace fcitx
