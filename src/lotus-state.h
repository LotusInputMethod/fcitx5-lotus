/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-state.h
 * @brief Input context state management for fcitx5-lotus.
 */

#ifndef _FCITX5_LOTUS_STATE_H_
#define _FCITX5_LOTUS_STATE_H_

#include "lotus.h"
#include "emoji-entry.h"
#include "lotus-utils.h"

#include <cstddef>
#include <fcitx-utils/misc.h>
#include <fcitx/inputcontext.h>

#include <atomic>

struct EmojiEntry;

namespace fcitx {
    class LotusEngine;
    class CommonCandidateList;
    class SurroundingText;

    /**
     * @brief Per-input-context state for Lotus input method.
     *
     * Manages the input state, buffers, and mode-specific handling for each input context.
     */
    class LotusState final : public InputContextProperty {
      public:
        /**
         * @brief Constructs a new state instance.
         * @param engine Pointer to the Lotus engine.
         * @param ic Pointer to the input context.
         */
        LotusState(LotusEngine* engine, InputContext* ic);

        /**
         * @brief Initializes the bamboo engine for this state.
         */
        void setEngine();

        /**
         * @brief Applies current options to the engine.
         */
        void setOption();

        /**
         * @brief Main key event handler.
         * @param keyEvent The key event to process.
         */
        void keyEvent(KeyEvent& keyEvent);

        /**
         * @brief Resets the input state.
         */
        void reset();

        /**
         * @brief Commits the current buffer.
         */
        void commitBuffer();

        /**
         * @brief Clears all internal buffers.
         */
        void clearAllBuffers();

        /**
         * @brief Checks if history buffer is empty.
         * @return True if no history.
         */
        bool isEmptyHistory();
        friend class EmojiCandidateWord;
        friend class LotusEngine;

      private:
        static constexpr size_t MAX_BUFFERED_KEYS = 50;

        LotusEngine*            engine_;
        InputContext*           ic_;
        CGoObject               lotusEngine_;
        std::string             oldPreBuffer_;
        std::string             history_;
        int                     expected_backspaces_     = 0;
        int                     current_backspace_count_ = 0;
        std::string             pending_commit_string_;
        std::atomic<int>        current_thread_id_{0};
        std::string             emojiBuffer_;
        std::vector<EmojiEntry> emojiCandidates_;
        bool                    waitAck_ = false;
        std::vector<KeyEntry>   buffered_keys_; ///< Keystrokes buffered during replacement
        bool                    isPrevSpace_ = false;

        /**
         * @brief Connects to the uinput server.
         * @return True if connection successful.
         */
        static bool connect_uinput_server();

        /**
         * @brief Sets up uinput device.
         * @return File descriptor or -1 on error.
         */
        static int setup_uinput();

        /**
         * @brief Sends backspace key events via uinput.
         * @param count Number of backspaces to send.
         */
        void send_backspace_uinput(int count) const;

        /**
         * @brief Replays buffer content to the engine.
         * @param buffer The string buffer to replay.
         */
        void replayBufferToEngine(const std::string& buffer);

        /**
         * @brief Checks if autofill is certain for surrounding text.
         * @param s The surrounding text.
         * @return True if autofill should proceed.
         */
        bool isAutofillCertain(const SurroundingText& s);

        /**
         * @brief Handles key events in preedit mode.
         * @param keyEvent The key event to process.
         */
        void handlePreeditMode(KeyEvent& keyEvent);

        /**
         * @brief Updates emoji page status in candidate list.
         * @param commonList The candidate list to update.
         */
        void updateEmojiPageStatus(CommonCandidateList* commonList);

        /**
         * @brief Handles key events in emoji mode.
         * @param keyEvent The key event to process.
         */
        void handleEmojiMode(KeyEvent& keyEvent);

        /**
         * @brief Updates preedit display for emoji mode.
         */
        void updateEmojiPreedit();

        /**
         * @brief Handles key press in uinput mode.
         * @param event The key event.
         * @param currentSym Current key symbol.
         * @param sleepTime Delay in microseconds.
         * @return True if event was handled.
         */
        bool handleUInputKeyPress(KeyEvent& event, KeySym currentSym, int sleepTime);

        /**
         * @brief Performs text replacement via uinput.
         * @param deletedPart Text to delete.
         * @param addedPart Text to insert.
         */
        void performReplacement(const std::string& deletedPart, const std::string& addedPart);

        /**
         * @brief Handles the double space to period replacement.
         */
        void handleDoubleSpaceReplacement();

        /**
         * @brief Checks and forwards special keys.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol (may be modified).
         */
        void checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym);

        /**
         * @brief Handles uinput mode processing.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         * @param checkEmptyPreedit Whether to check for empty preedit.
         * @param sleepTime Delay in microseconds.
         */
        void handleUinputMode(KeyEvent& keyEvent, KeySym currentSym, bool checkEmptyPreedit, int sleepTime);

        /**
         * @brief Handles surrounding text mode.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         */
        void handleSurroundingText(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Handles processing normal key events.
         * @param keyEvent The key event.
        */
        void processNormalKey(KeyEvent& keyEvent);

        /**
         * @brief Replays keystrokes buffered during replacement.
         *
         * When is_deleting_ is true, non-special keystrokes are buffered
         * instead of being discarded. This method replays them after the
         * replacement completes.
         */
        void replayBufferedKeys();
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_STATE_H_
