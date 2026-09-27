/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file emoji.h
 * @brief Emoji search and loading utilities.
 */

#ifndef EMOJI_H
#define EMOJI_H

#include "emoji-entry.h"

#include <string>
#include <vector>

#include <fcitx/addonmanager.h>

/**
 * @brief Emoji loader with fuzzy search capability.
 *
 * Loads emoji list from Fcitx5 emoji addon and provides fuzzy matching search.
 */
class EmojiLoader {
  public:
    /**
     * @brief Constructs loader and initializes emoji list from Fcitx5.
     *
     * The emoji list is loaded for the default language "en" (see
     * loadFromFcitx5).
     *
     * @param addonManager Fcitx5 addon manager instance.
     */
    EmojiLoader(fcitx::AddonManager* addonManager);

    /**
     * @brief Searches emoji by prefix with fuzzy matching.
     * @param prefix Search query.
     * @return Sorted list of matching emojis.
     */
    std::vector<EmojiEntry> search(const std::string& prefix);

    /**
     * @brief Records an emoji into history.
     * @param entry The emoji entry to record.
     */
    void recordHistory(const EmojiEntry& entry);

    /**
     * @brief Gets the emoji history.
     * @return List of recently used emojis.
     */
    const std::vector<EmojiEntry>& history() const {
        return historyList;
    }

    /**
     * @brief Gets total emoji count.
     * @return Number of emojis loaded.
     */
    size_t size() const;

  private:
    std::vector<EmojiEntry> historyList;   ///< Recently used emojis
    std::vector<EmojiEntry> emojiList;     ///< Internal emoji storage
    fcitx::AddonInstance*   emojiAddon_{}; ///< Fcitx5 emoji addon
    std::string             historyPath_;

    /**
     * @brief Load emoji data from Fcitx5 emoji addon.
     * @param language Language code for emoji data.
     */
    void loadFromFcitx5(const std::string& language = "en");

    /**
     * @brief Loads emoji history from disk.
     */
    void loadHistory();

    /**
     * @brief Saves emoji history to disk.
     */
    void saveHistory();
};

#endif // EMOJI_H