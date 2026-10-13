/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file key_processor.h
 * @brief Key event wrapper around the engine, ported 1:1 from bamboo/fcitxbambooengine.go.
 *
 * The Go file is the fcitx glue: it decides which keys reach the engine, when the composition is
 * committed, and how macros, the output charset and the automatic English restore are applied. The
 * port keeps that logic in one class with no fcitx dependency so it can be tested headless. Key
 * values stay in the fcitx keysym domain because Go compares the raw keyVal with
 * FcitxBackSpace/FcitxSpace/FcitxTab, and the texts kept by the processor are UTF-32 like the rest
 * of the engine. UTF-8 only appears at the addon boundary and while loading data: MacroTable::set and
 * Dictionary::loadFromWords/loadFromText decode the configuration strings once, lotus::encode writes
 * the legacy charsets and formatTime renders the layout of the time macros.
 */

#ifndef _LOTUS_KEY_PROCESSOR_H_
#define _LOTUS_KEY_PROCESSOR_H_

#include "dictionary.h"
#include "encoder.h"
#include "engine.h"
#include "macro_table.h"
#include "rule.h"
#include "types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lotus {

    /** @brief Keysyms of the special keys the processor reacts to, 1:1 with Go's constants. */
    inline constexpr uint32_t kFcitxBackSpace = 0xFF08U;
    inline constexpr uint32_t kFcitxSpace     = 0x020U;
    inline constexpr uint32_t kFcitxTab       = 0xFF09U;

    /** @brief Case of the word that triggered a macro, 1:1 with Go's VnCase* constants. */
    enum class MacroCase : uint8_t {
        AllSmall   = 1, // Every letter is lowercase
        AllCapital = 2, // Every letter is uppercase
        NoChange   = 3, // Mixed case, or no letter at all
    };

    /**
     * @brief Case of a macro trigger word, like Go's determineMacroCase.
     * @param text Trigger word.
     * @return AllSmall when every letter is lowercase, AllCapital when every letter is uppercase,
     *         NoChange for a mixed word or a word without letters.
     */
    MacroCase determineMacroCase(std::u32string_view text);

    /**
     * @brief Last code point of a text, like Go's getLastRune.
     * @param text Input text.
     * @return Last code point, 0 for an empty text.
     */
    char32_t getLastRune(std::u32string_view text);

    /**
     * @brief Reports whether a key is in a key list, like Go's inKeyList.
     * @param list Key list.
     * @param key Input key.
     * @return True when key appears in list.
     */
    bool inKeyList(const std::vector<char32_t>& list, char32_t key);

    /** @brief Options of the processor, 1:1 with Go's FcitxBambooEngineOption (bamboo-c.go). */
    struct Options {
        bool        autoNonVnRestore    = true;                         // Restore the raw key strokes of a non-Vietnamese word
        bool        ddFreeStyle         = true;                         // Allow "dd" inside a non-Vietnamese sequence
        bool        macroEnabled        = false;                        // Expand macros at a word break and on Tab
        bool        autoCapitalizeMacro = false;                        // Follow the case of the trigger word
        bool        spellCheckWithDicts = true;                         // Spell check against the custom dictionary
        bool        modernStyle         = true;                         // "hòa" instead of "hoà"
        bool        freeMarking         = true;                         // Free tone marking style
        std::string outputCharset       = std::string(kUnicodeCharset); // Charset of the committed text
        // Empty optional = follow the engine flags, the mode a fresh engine starts in. The addon always
        // fills them from the configuration, so it only ever sees the three public enumerators.
        std::optional<W2uMode>              w2u              = std::nullopt; // "w" to "ư" mode
        std::optional<BracketTransformMode> bracketTransform = std::nullopt; // "[ -> ơ, ] -> ư" mode
        std::string                         timeFormat       = "%H:%M";      // Format of the $TIME macro
        std::string                         dateFormat       = "%d/%m/%Y";   // Format of the $DATE macro
    };

    /**
     * @brief Key event wrapper driving an Engine, like Go's FcitxBambooEngine.
     *
     * The processor owns its engine, shares the macro table and the dictionary with the other input
     * contexts through std::shared_ptr and keeps no raw pointer, so it stays copyable and movable.
     * The methods that mirror one step of processKey are public because Go has no visibility rules
     * and the ported tests drive them directly.
     */
    class KeyProcessor {
      public:
        /**
         * @brief Builds a processor, like Go's NewEngine / NewCustomEngine.
         * @param inputMethod Input method of the underlying engine.
         * @param spellCheckWithDictsDefault Initial value of Options::spellCheckWithDicts (Go's
         *                                   NewEngine passes true, NewCustomEngine passes false).
         */
        KeyProcessor(InputMethod inputMethod, bool spellCheckWithDictsDefault);

        /**
         * @brief Applies a whole option set, like Go's EngineSetOption.
         *
         * The engine flags are recomputed from modernStyle, freeMarking, w2u and bracketTransform
         * exactly like the Go export does, and both modes are pushed to the engine.
         * @param options New options.
         */
        void setOptions(const Options& options);

        /**
         * @brief Options in use.
         *
         * Go's tests set the FcitxBambooEngine fields directly, so the port exposes the option
         * struct for writing too. The engine flags and the w2u / bracket modes are only recomputed
         * by setOptions, which is what the addon calls when the configuration changes.
         * @return Mutable options.
         */
        [[nodiscard]] Options& options();

        /**
         * @brief Options in use.
         * @return Options.
         */
        [[nodiscard]] const Options& options() const;

        /**
         * @brief Enables or disables macro expansion, like Go's EngineSetMacroEnabled.
         * @param enabled New value of Options::macroEnabled.
         */
        void setMacroEnabled(bool enabled);

        /**
         * @brief Arms the "restore key strokes" state of the Shift+Space workaround, like the Go field.
         * @param restore True to make the next key event restore the raw key strokes.
         */
        void setShouldRestoreKeyStrokes(bool restore);

        /**
         * @brief Reports whether the Shift+Space workaround is armed, like the Go field.
         * @return True when the next key event restores the raw key strokes.
         */
        [[nodiscard]] bool shouldRestoreKeyStrokes() const;

        /**
         * @brief Shares a macro table with this processor.
         * @param table Macro table, shared with the other input contexts (null disables the lookups).
         */
        void setMacroTable(std::shared_ptr<const MacroTable> table);

        /**
         * @brief Shares a custom dictionary with this processor.
         * @param dictionary Dictionary, shared with the other input contexts (null disables the lookups).
         */
        void setDictionary(std::shared_ptr<const Dictionary> dictionary);

        /**
         * @brief Processes one key event, like Go's preeditProcessKeyEvent.
         * @param keyVal Keysym of the key (kFcitxBackSpace / kFcitxSpace / kFcitxTab / a code point).
         * @param states Modifier states of the event.
         * @return True when the processor handled the key, false when the addon must let it through.
         */
        bool processKey(uint32_t keyVal, KeyStates states);

        /**
         * @brief Reports whether the processor wants a key event, like Go's canProcessKey.
         * @param keyVal Keysym of the key.
         * @return True for space, backspace, a word break symbol, a macro Tab or an engine key.
         */
        [[nodiscard]] bool canProcessKey(uint32_t keyVal) const;

        /**
         * @brief Reports whether a modifier mask is acceptable, like Go's isValidState.
         * @param states Modifier states of the event.
         * @return False when control, alt, super, hyper, meta or the forward modifier is held.
         */
        [[nodiscard]] static bool isValidState(KeyStates states);

        /**
         * @brief Preedit text to display, like Go's preeditText.
         * @return Charset encoded preedit text.
         */
        [[nodiscard]] const std::u32string& preedit() const;

        /**
         * @brief Takes the text to commit and clears it, like Go's EnginePullCommit.
         * @return Charset encoded text to commit, empty when there is nothing to commit.
         */
        std::u32string takeCommit();

        /**
         * @brief Commits the current preedit text, like Go's EngineCommitPreedit.
         */
        void commitPreedit();

        /**
         * @brief Forgets the composition and the pending commit, like Go's ResetEngine.
         */
        void reset();

        /**
         * @brief Rebuilds the composition from a text, like Go's EngineRebuildFromText.
         * @param text Text to rebuild from.
         */
        void rebuildFromText(std::u32string_view text);

        /**
         * @brief Engine driven by this processor.
         * @return Engine.
         */
        [[nodiscard]] Engine& engine();

        /**
         * @brief Formats the current date or time, like Go's formatTime.
         *
         * Go's strftimeReplacer table is applied first (it turns the format into a Go reference
         * layout), then the layout is translated to the equivalent std::strftime pattern; a format
         * that keeps its '%' falls back to the reference layout "15:04:05 02/01/2006".
         * @param format Format configured for the $TIME / $DATE macro.
         * @return Formatted current local time, empty for an empty format.
         */
        [[nodiscard]] static std::string formatTime(std::string_view format);

        /**
         * @brief Expands a macro value, like Go's expandMacro.
         * @param str Trigger word (drives the case when autoCapitalizeMacro is set).
         * @param macroText Macro value; "$TIME" and "$DATE" are replaced when the matching format
         *                  is configured.
         * @return Expanded text.
         */
        [[nodiscard]] std::u32string expandMacro(std::u32string_view str, std::u32string_view macroText) const;

        /**
         * @brief Stores the encoded preedit text and clears the pending commit, like Go's updatePreedit.
         * @param processedStr Rendered text; an empty one clears both texts.
         */
        void updatePreedit(std::u32string_view processedStr);

        /**
         * @brief Stores a text to commit and resets the engine, like Go's commitPreeditAndReset.
         * @param text Text to commit.
         */
        void commitPreeditAndReset(std::u32string text);

        /**
         * @brief Reports whether the typed word must be restored to raw key strokes at a word break,
         *        like Go's mustFallbackToEnglish.
         * @return True when the word is invalid and the dictionary does not know it.
         */
        [[nodiscard]] bool mustFallbackToEnglish() const;

        /**
         * @brief Reports whether the composition must be restored to English, like Go's
         *        shouldFallbackToEnglish.
         * @param checkVnRune True to require a Vietnamese code point before falling back.
         * @return True when the engine rejects the current sequence.
         */
        [[nodiscard]] bool shouldFallbackToEnglish(bool checkVnRune) const;

        /**
         * @brief Uppercases a key for the bracket transform, like Go's toUpper.
         * @param keyRune Input key.
         * @return '{' for '[' and '}' for ']' when brackets are transformed, the reverse mapping
         *         for '{' / '}'; every other key is returned unchanged.
         */
        [[nodiscard]] char32_t toUpper(char32_t keyRune) const;

      private:
        /**
         * @brief Processes one key event without the last-key bookkeeping, like Go's
         *        preeditProcessKeyEvent (processKey adds the deferred updateLastKeyWithShift).
         * @param keyVal Keysym of the key.
         * @param states Modifier states of the event.
         * @return True when the processor handled the key.
         */
        bool preeditProcessKeyEvent(uint32_t keyVal, KeyStates states);

        /**
         * @brief Builds the text a key previews or commits, like Go's getCommitText.
         * @param keyVal Keysym of the key.
         * @param states Modifier states of the event.
         * @param oldText Preedit text before the key.
         * @return Text to display or commit, and whether it is a word break commit.
         */
        [[nodiscard]] std::pair<std::u32string, bool> getCommitText(uint32_t keyVal, KeyStates states, std::u32string_view oldText);

        /**
         * @brief Encodes a text with the configured charset, like Go's encodeText.
         * @param text Text to encode.
         * @return Encoded text.
         */
        [[nodiscard]] std::u32string encodeText(std::u32string_view text) const;

        /**
         * @brief Macro value of the current composition, like Go's getMacroText.
         * @return Expanded macro text, std::nullopt when macros are off or the key has no macro.
         */
        [[nodiscard]] std::optional<std::u32string> getMacroText() const;

        /**
         * @brief Renders a part of the composition, like Go's getProcessedString.
         * @param mode Output mode.
         * @return Rendered text.
         */
        [[nodiscard]] std::u32string getProcessedString(ModeSet mode) const;

        /**
         * @brief Text of the composition before encoding, like Go's getPreeditString.
         * @return Rendered text (English mode when the sequence falls back).
         */
        [[nodiscard]] std::u32string getPreeditString() const;

        /**
         * @brief Text to commit at a word break, like Go's getComposedString.
         * @param oldText Preedit text before the key.
         * @return English rendering when the word must be restored, oldText otherwise.
         */
        [[nodiscard]] std::u32string getComposedString(std::u32string_view oldText) const;

        /**
         * @brief Number of code points of the preedit text, like Go's runeCount.
         * @return Code point count.
         */
        [[nodiscard]] std::size_t runeCount() const;

        /**
         * @brief Mode handed to the engine for the current sequence, like Go's getBambooInputMode.
         * @return EnglishMode when the sequence falls back, VietnameseMode otherwise.
         */
        [[nodiscard]] ModeSet getBambooInputMode() const;

        /**
         * @brief Remembers whether the last processed key was typed with Shift, like the Go method.
         * @param keyVal Keysym of the key.
         * @param states Modifier states of the event.
         */
        void updateLastKeyWithShift(uint32_t keyVal, KeyStates states);

        /**
         * @brief Reports whether the macro table is missing or empty, like `e.macroTable.Empty()`
         *        (a null std::shared_ptr stands for Go's nil table).
         * @return True when there is no macro to look up.
         */
        [[nodiscard]] bool macroTableEmpty() const;

        /**
         * @brief Replaces every occurrence of a needle, like Go's strings.ReplaceAll.
         * @param text Input text.
         * @param needle Text to replace; an empty one returns text unchanged.
         * @param replacement Replacement text.
         * @return Text with every occurrence replaced.
         */
        [[nodiscard]] static std::u32string replaceAllText(std::u32string_view text, std::u32string_view needle, std::u32string_view replacement);

        /**
         * @brief Applies Go's strftimeReplacer table to a format, like the Go variable.
         * @param format User format ("%H:%M", ...).
         * @return Go reference layout ("15:04", ...), unchanged when no placeholder matched.
         */
        [[nodiscard]] static std::string goTimeLayout(std::string_view format);

        /**
         * @brief Translates a Go reference layout to a std::strftime pattern.
         * @param layout Go reference layout.
         * @return strftime pattern ("%H:%M", ...).
         */
        [[nodiscard]] static std::string strftimePattern(std::string_view layout);

        // Go: the FcitxBambooEngine fields.
        Engine                            engine_;
        Options                           options_;
        std::shared_ptr<const MacroTable> macroTable_;
        std::shared_ptr<const Dictionary> dictionary_;
        std::u32string                    preedit_;
        std::u32string                    commit_;
        bool                              lastKeyWithShift_        = false;
        bool                              shouldRestoreKeyStrokes_ = false;
    };

} // namespace lotus

#endif // _LOTUS_KEY_PROCESSOR_H_
