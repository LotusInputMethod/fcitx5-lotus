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
#include "lotus-key-buffer.h"
#include "common-protocol.h"
#include "lotus-utils.h"

#include <chrono>
#include <cstddef>
#include <fcitx-utils/event.h>
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
         * @param isFocusOut If true, the reset comes from a focus-out event. Focus-out never commits text: fcitx5 core or the client (CapabilityFlag::ClientUnfocusCommit) owns that commit.
         */
        void reset(bool isFocusOut = false);

        /**
         * @brief Applies the reset that a focus-out deferred in uinput modes (the focus really moved away).
         */
        void flushDeferredFocusOut();

        /**
         * @brief Drops the reset deferred by a focus-out (the focus-out was a bounce).
         */
        void cancelDeferredFocusOut();

        /**
         * @brief Finishes a replacement whose backspace echoes were lost (focus-out mid pump).
         */
        void finishOverdueReplacement();

        /**
         * @brief Commits the current buffer.
         */
        void commitBuffer();

        /**
         * @brief Clears all internal buffers.
         * @param keepSentenceState Keep the sentence state (shouldCapitalize_, isPrevPunctuation_)
         */
        void clearAllBuffers(bool keepSentenceState = false);

        /**
         * @brief Checks if history buffer is empty.
         * @return True if no history.
         */
        [[nodiscard]] bool isEmptyHistory() const;
        friend class EmojiCandidateWord;
        friend class LotusEngine;

      private:
        LotusEngine&                          engine_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        InputContext&                         ic_;     // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        std::shared_ptr<lotus::KeyProcessor>  lotusEngine_;
        std::u32string                        oldPreBuffer_;
        bool                                  hasHistory_            = false;
        size_t                                expectedBackspaces_    = 0;
        size_t                                currentBackspaceCount_ = 0;
        bool                                  realSelect_            = false; ///< when real shift +left is send
        bool                                  lastSelect_            = false; ///< kind of the most recent burst, for replay
        std::u32string                        pendingCommitString_;
        std::u32string                        commitBatch_;                   ///< Text of the replacement batch, sent as one commitString
        std::unique_ptr<EventSourceTime>      deferredCommitTimer_;           ///< Pending dbus commit timer
        std::u32string                        deferredCommitText_;            ///< Text waiting for the deferred commit
        bool                                  deferredCommitPending_ = false; ///< Timer armed and not fired yet
        bool                                  deferCommits_          = false; ///< Route the batch through the deferred timer
        LotusAppRuleSetting                   appRuleSetting_;                ///< Per-app rule (mode + delay overrides) pushed by the engine
        std::u32string                        emojiBuffer_;
        std::vector<EmojiEntry>               emojiCandidates_;
        KeyBuffer                             bufferedKeys_; ///< Keystrokes buffered during replacement
        bool                                  isPrevSpace_           = false;
        bool                                  isPrevHyphen_          = false;
        bool                                  shouldCapitalize_      = false;
        bool                                  isPrevPunctuation_     = false;
        int64_t                               lastDeactivateTime_    = 0;
        int64_t                               deletionInterruptedAt_ = 0;     ///< when deactivate() cut an in-flight replacement (0 = none)
        bool                                  deferredFocusOut_      = false; ///< uinput focus-out reset waiting for activate()
        bool                                  trackingModifierTap_   = false; ///< Selected modifier held, waiting for consecutive keyup
        bool                                  shiftHeld_             = false; ///< Live Shift press state tracked from key events
        bool                                  macroSkip_             = false; ///< Macro disabled for the current word
        bool                                  isLibreoffice_         = false;
        bool                                  isChromium_            = false;
        int64_t                               replacementDeadlineMs_ = 0; ///< ms by which every injected backspace must be echoed (0 = no replacement in flight)
        int64_t                               replacementEchoGrace_  = 0; ///< deadline grace re-armed on each echo
        std::chrono::steady_clock::time_point lastCommitTime_;

        // Replacement aborted at focus-out, committed once focus returns.
        bool           uinputAbortPending_ = false; ///< aborted replacement awaiting its commit
        std::u32string stashedCommit_;              ///< replacement text that must not be lost
        std::u32string stashedNewWord_;             ///< post-replacement word (oldPreBuffer_ at abort time)
        int64_t        uinputAbortTimeMs_ = 0;      ///< when the abort happened

        /**
         * @brief Performs a reset without the uinput focus-out deferral.
         * @param isFocusOut True when the reset comes from a focus-out event. Only cleans up composition state; it never commits text.
         */
        void resetNow(bool isFocusOut);

        /**
         * @brief Sets up uinput device.
         * @return True when a usable keyboard socket is held.
         */
        bool setupUinput();

        /**
         * @brief Sends a keyboard request to the uinput server (reconnect on failure).
         * @param op Operation to request.
         * @param count Number of backspaces or characters to select.
         * @param preDelay Delay in milliseconds before executing the keyboard operation.
         * @param interval Delay in milliseconds between consecutive injected keys.
         * @param postDelay Delay in milliseconds after completing the keyboard operation.
         */
        void sendKbMsg(KbOp op, size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay);

        /**
         * @brief Sends backspace key events via uinput.
         * @param count Number of backspaces to send.
         * @param preDelay Delay in milliseconds before executing the keyboard operation.
         * @param interval Delay in milliseconds between consecutive injected keys.
         * @param postDelay Delay in milliseconds after completing the keyboard operation.
         */
        void sendBackspaceUinput(size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay);

        /**
         * @brief Asks the uinput server to select count characters with Shift+Left.
         * @param count Number of characters to select.
         * @param preDelay Delay in milliseconds before executing the keyboard operation.
         * @param interval Delay in milliseconds between consecutive injected keys.
         * @param postDelay Delay in milliseconds after completing the keyboard operation.
         */
        void sendSelectUinput(size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay);

        /**
         * @brief Sends Cancel to drop the keys still pending from the in-flight replacement.
         */
        void cancelPendingUinput();

        /**
         * @brief Cancels the in-flight burst and stashes the replacement text for the refocus commit.
         */
        void abortPendingUinputReplacement();

        /**
         * @brief Commits the replacement text stashed at focus-out; stale stashes are dropped.
         */
        void reconcileAbortedUinputReplacement();

        /**
         * @brief Drops the stash of an aborted replacement and replays buffered keys.
         */
        void dropAbortedUinputStash();

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
        void ensureUinputConnection();

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
         * @brief Completes an in-flight replacement after all echoed events arrived.
         *
         * Shared tail of the BackSpace and Shift+Left echo handlers: waits for the
         * app to settle, commits the pending replacement string, resets the
         * replacement state, swallows the final echo and replays buffered keys.
         *
         * @param event The key event of the final echoed key.
         */
        void finishReplacement(KeyEvent& event);

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
         * @brief Appends text to the batch that goes out as a single commitString.
         * @param text Text to append.
         */
        void appendCommit(std::u32string_view text);

        /**
         * @brief Sends the batch as one commitString, immediately or through the deferred timer.
         */
        void flushCommitBatch();

        /**
         * @brief Arms the deferred commit timer when it is not armed yet.
         */
        void scheduleDeferredCommit();

        /**
         * @brief Delivers text that is still waiting for the deferred timer, then the current batch.
         */
        void deliverPendingCommits();

        /**
         * @brief Replays keystrokes buffered during replacement.
         *
         * When g_isDeleting is true, non-special keystrokes are buffered
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
         * @brief Updates the live Shift press state from a Shift key event.
         *
         * fcitx5 exposes modifiers only per key event, so the addon keeps the
         * running press state itself to answer "is Shift held right now" at
         * replay time.
         * @param keyEvent The Shift_L/Shift_R press or release event.
         */
        void trackShiftModifier(const KeyEvent& keyEvent);

        /**
         * @brief Reports whether Shift is currently held, from tracked events.
         * @return True while either Shift key is pressed without its release.
         */
        [[nodiscard]] bool isShiftHeld() const;

        /**
         * @brief Forgets the tracked Shift state, e.g. after a focus-out may have swallowed a release.
         */
        void forgetShiftState();

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
