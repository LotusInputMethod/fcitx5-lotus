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

#include "emoji-entry.h"
#include "key_processor.h"
#include "common-protocol.h"
#include "lotus-protocol.h"
#include "lotus-utils.h"

#include <chrono>
#include <cstddef>
#include <fcitx/inputcontext.h>
#include <memory>

namespace fcitx {
    class LotusEngine;
    class CommonCandidateList;
    class SurroundingText;

    /**
     * @brief Clamps a delay read from the configuration to the supported range.
     * @param value Delay in milliseconds.
     * @return The delay clamped to the supported range.
     */
    int clampDelay(int value);

    /**
     * @brief Whether a code point breaks a word.
     * @param ucs4 Code point to test.
     * @return True when the code point breaks a word.
     */
    bool isWordBreak(uint32_t ucs4);

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
         * @brief Unregisters from the pacer barrier owner on teardown so a
         * parked wait can never fire into a destroyed state.
         */
        ~LotusState();

        /**
         * @brief Fires a chain Commit item: commits and completes.
         *
         * Shared tail of every replacement mode (Select and backspace both
         * end in a chain barrier now): commits, resets the echo state,
         * clears is_deleting_ and replays buffered keys. Called on the pump
         * via the barrier owner, never directly from key handling. The chain
         * text is authoritative: re-stated into the pending string so no
         * stale value can substitute it.
         */
        void commitFromChain(const std::string& text);

        /**
         * @brief Current counted-echo sequence for barrier waits.
         */
        [[nodiscard]] uint32_t echoSequence() const;

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
         * @param isFocusOut If true, indicates the reset is due to a focus-out event, which may trigger committing the preedit text.
         */
        void reset(bool isFocusOut = false);

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
        [[nodiscard]] bool isEmptyHistory() const;
        friend class EmojiCandidateWord;
        friend class LotusEngine;

      private:
        static constexpr size_t               MAX_BUFFERED_KEYS = 50;

        LotusEngine&                          engine_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        InputContext&                         ic_;     // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        std::shared_ptr<lotus::KeyProcessor>  lotusEngine_;
        std::u32string                        oldPreBuffer_;
        bool                                  hasHistory_              = false;
        size_t                                expected_backspaces_     = 0;
        size_t                                current_backspace_count_ = 0;
        std::u32string                        pending_commit_string_;
        LotusAppRuleSetting                   appRuleSetting_; ///< Per-app rule (mode + delay overrides) pushed by the engine
        std::u32string                        emojiBuffer_;
        std::vector<EmojiEntry>               emojiCandidates_;
        std::vector<KeyEntry>                 buffered_keys_; ///< Keystrokes buffered during replacement
        bool                                  isPrevSpace_           = false;
        bool                                  isPrevHyphen_          = false;
        bool                                  shouldCapitalize_      = false;
        bool                                  isPrevPunctuation_     = false;
        int64_t                               lastDeactivateTime_    = 0;
        int64_t                               deletionInterruptedAt_ = 0;     ///< when deactivate() cut an in-flight replacement (0 = none)
        bool                                  tracking_modifier_tap_ = false; ///< Selected modifier held, waiting for consecutive keyup
        bool                                  macro_skip_            = false; ///< Macro disabled for the current word
        std::chrono::steady_clock::time_point last_commit_time_;

        /**
         * @brief Ensures the uinput server connection via the shared KeyPacer.
         * @return True if the server is reachable (best-effort connect).
         */
        static bool ensureUinputConnected();

        /**
         * @brief Queues N backspace taps paced by EventLoop timers, ending in
         * a chain commit.
         * @param count Number of backspace taps (also the echo barrier target).
         * @param pre_delay Ms to wait before the first tap (commit cooldown).
         * @param interval Ms between consecutive taps.
         * @param post_delay Ms to wait before the final tap.
         * @param text Replacement text carried by the chain Commit item.
         */
        static void enqueueBackspace(size_t count, uint32_t pre_delay, uint32_t interval, uint32_t post_delay, std::string text);

        /**
         * @brief Queues an N-character Shift+Left selection ending in a chain commit.
         * @param count Number of characters to select.
         * @param pre_delay Ms to wait before Shift-down.
         * @param interval Ms between consecutive Left taps (also the settle
         * gap before Delete).
         * @param post_delay Ms to wait after the commit.
         * @param text Replacement text carried by the chain Commit item.
         */
        static void enqueueSelect(size_t count, uint32_t pre_delay, uint32_t interval, uint32_t post_delay, std::string text);

        /**
         * @brief Handles the pre-dispatch guards of a key event (engine presence, shift in
         * preedit mode, modifier taps and key releases).
         * @param keyEvent The key event to inspect.
         * @return True when the event must not be processed any further.
         */
        bool handleKeyEventGuards(KeyEvent& keyEvent);

        /**
         * @brief Reconnects to the uinput server when the client socket is not open.
         */
        static void ensureUinputConnection();

        /**
         * @brief Finishes an in-flight replacement once every echoed backspace arrived.
         */
        void completePendingReplacement();

        /**
         * @brief Applies auto-capitalization state to the pressed key.
         * @param keyEvent The key event, which may be rewritten with an upper case key.
         * @param currentSym The current key symbol, replaced by its upper case variant.
         */
        void applyAutoCapitalize(KeyEvent& keyEvent, KeySym& currentSym);

        /**
         * @brief Handles keys received while a replacement is still deleting text.
         * @param keyEvent The key event.
         * @param currentSym The current key symbol.
         * @return True when the event was consumed.
         */
        bool handleKeyWhileDeleting(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Handles the double space to period shortcut.
         * @param keyEvent The key event.
         * @param currentSym The current key symbol.
         * @return True when the shortcut fired and the event was consumed.
         */
        bool handleDoubleSpaceToPeriod(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Handles the double hyphen to em-dash shortcut.
         * @param keyEvent The key event.
         * @param currentSym The current key symbol.
         * @return True when the shortcut fired and the event was consumed.
         */
        bool handleDoubleHyphenToEmDash(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Dispatches the key event to the handler of the active mode.
         * @param keyEvent The key event.
         * @param currentSym The current key symbol.
         */
        void handleActiveMode(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Checks if autofill is certain for surrounding text.
         * @param s The surrounding text.
         * @return True if autofill should proceed.
         */
        bool isAutofillCertain(const SurroundingText& s);

        /**
         * @brief Handles key events in preedit mode.
         * @param keyEvent The key event to process.
         * @param currentSym Current key symbol.
         */
        void handlePreeditMode(KeyEvent& keyEvent, KeySym currentSym);

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
         * @return True if event was handled.
         */
        bool handleUInputKeyPress(KeyEvent& event, KeySym currentSym);

        /**
         * @brief Validates the app state and commits the pending string.
         *
         * Shared head of both replacement completions; changes no replacement
         * state besides consuming the pending string.
         */
        void commitPendingReplacement();

        /**
         * @brief Performs text replacement via uinput.
         * @param diffPart pairs of deleted part and added part
         */
        void performReplacement(DiffPart diffPart);

        /**
         * @brief Replaces the last character, either through surrounding text or through uinput.
         * @param diff pairs of deleted part and added part
         * @param logMsg Message to log once the replacement is issued.
         */
        void commitOrReplace(DiffPart diffPart, const std::string& logMsg);

        /**
         * @brief Forwards special keys (cursor move, Tab, Escape, modifiers) and reports
         * whether the event is consumed.
         * @param keyEvent The key event.
         * @param currentSym The current key symbol.
         * @return True when the event was forwarded.
         */
        bool forwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym);

        /**
         * @brief Reads the text the engine wants to commit.
         * @return The pending commit text, empty when there is none.
         */
        std::u32string pullCommitString();

        /**
         * @brief Reads the current engine preedit.
         * @return The preedit text, empty when there is none.
         */
        std::u32string pullPreeditString();

        /**
         * @brief Handles the double space to period replacement.
         */
        void handleDoubleSpaceReplacement();

        /**
         * @brief Handles the double hyphen to em-dash replacement.
         */
        void handleDoubleHyphenReplacement();

        /**
         * @brief Checks and forwards special keys.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol (may be modified).
         * @return True if key was forwarded.
         */
        bool checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym);

        /**
         * @brief Handles uinput mode processing.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         */
        void handleUinputMode(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Handles surrounding text mode.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         */
        void handleSurroundingText(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Handles Off mode with macro shadow processing.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         */
        void handleOffModeMacro(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Handles processing normal key events.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         */
        void processNormalKey(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Replays keystrokes buffered during replacement.
         *
         * When is_deleting_ is true, non-special keystrokes are buffered
         * instead of being discarded. This method replays them after the
         * replacement completes.
         */
        void replayBufferedKeys();

        /**
         * @brief Checks if the key symbol matches the configured macro-skip modifier.
         * @param sym Key symbol to check.
         * @return True if the key is the configured trigger modifier (left/right same).
         */
        [[nodiscard]] bool isMacroSkipModifier(KeySym sym) const;

        /**
         * @brief Tracks a modifier tap (keydown then consecutive keyup) to skip macro.
         * @param keyEvent The modifier key event.
         */
        void handleModifierTap(const KeyEvent& keyEvent);

        /**
         * @brief Cancels an in-progress modifier tap when another key arrives.
         */
        void cancelModifierTap();

        /**
         * @brief Re-enables macro when the current word ends (engine preedit empty).
         */
        void reEnableMacroAfterWordEnd();

        /**
         * @brief Clears the macro-skip state and re-syncs the engine.
         */
        void resetMacroSkip();

        /**
         * @brief get unshift key of a key symbol
         * @param sym Key symbol need to unshift
         * @return The unshifted key symbol
         */
        static KeySym unshiftKeySym(KeySym sym);
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_STATE_H_
