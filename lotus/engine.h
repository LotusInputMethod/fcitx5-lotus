/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file engine.h
 * @brief Composition based input engine, ported 1:1 from bamboo-core/bamboo.go.
 *
 * Go exposes an IEngine interface next to the BambooEngine struct; the port keeps a single class
 * because that interface only existed for the cgo binding.
 */

#ifndef _LOTUS_ENGINE_H_
#define _LOTUS_ENGINE_H_

#include "composition.h"
#include "engine_utils.h"
#include "rule.h"
#include "transformation.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lotus {

    /**
     * @brief Turns key strokes into a Vietnamese composition and renders it back.
     */
    class Engine {
      public:
        /**
         * @brief Builds an engine, like Go's NewEngine.
         * @param inputMethod Input method used to transform keys.
         * @param flags Engine flags (EngineFlag::…).
         */
        Engine(InputMethod inputMethod, EngineFlags flags);

        /**
         * @brief Input method of the engine, like Go's GetInputMethod.
         * @return Input method.
         */
        [[nodiscard]] const InputMethod& inputMethod() const;

        /**
         * @brief Replaces the engine flags, like Go's SetFlag.
         * @param flag New engine flags.
         */
        void setFlag(EngineFlags flag);

        /**
         * @brief Engine flags, like Go's GetFlag.
         * @return Engine flags.
         */
        [[nodiscard]] EngineFlags flag() const;

        /**
         * @brief Sets the "w" to "ư" mode, like Go's SetW2UMode.
         * @param mode Mode to apply; an empty optional keeps following the W2uEnabled engine flag,
         *             which is the state a fresh engine starts in.
         */
        void setW2uMode(std::optional<W2uMode> mode);

        /**
         * @brief Sets the bracket transform mode, like Go's SetBracketTransformMode.
         * @param mode Mode to apply; an empty optional keeps following the
         *             BracketTransformEnabled engine flag, the state a fresh engine starts in.
         */
        void setBracketTransformMode(std::optional<BracketTransformMode> mode);

        /**
         * @brief Validates the last word, like Go's IsValid.
         * @param inputIsFullComplete Whether the word is complete.
         * @return True when the last word is a valid Vietnamese syllable.
         */
        [[nodiscard]] bool isValid(bool inputIsFullComplete) const;

        /**
         * @brief Renders a part of the composition, like Go's GetProcessedString.
         * @param mode Output mode, including FullText / PunctuationMode.
         * @return Rendered text.
         */
        [[nodiscard]] std::u32string processedString(ModeSet mode) const;

        /**
         * @brief Reports whether the engine transforms a key, like Go's CanProcessKey.
         * @param key Key to test (any case).
         * @return True when the engine processes the key.
         */
        [[nodiscard]] bool canProcessKey(char32_t key) const;

        /**
         * @brief Processes every code point of a text, like Go's ProcessString.
         * @param text Input text.
         * @param mode Typing mode.
         */
        void processString(std::u32string_view text, ModeSet mode);

        /**
         * @brief Processes one key stroke, like Go's ProcessKey.
         * @param key Input key (any case).
         * @param mode Typing mode.
         */
        void processKey(char32_t key, ModeSet mode);

        /**
         * @brief Restores the last word, like Go's RestoreLastWord.
         * @param toVietnamese True to re-process the word as Vietnamese, false to break it back to raw
         *                     key strokes.
         */
        void restoreLastWord(bool toVietnamese);

        /**
         * @brief Forgets the composition, like Go's Reset.
         */
        void reset();

        /**
         * @brief Drops the last appending node and its effects, like Go's RemoveLastChar.
         * @param refreshLastToneTarget Whether to move the tone to the new last vowel.
         */
        void removeLastChar(bool refreshLastToneTarget);

        /**
         * @brief Rebuilds the composition from a text, like Go's RebuildEngineFromText.
         * @param text Text to rebuild from.
         */
        void rebuildFromText(std::u32string_view text);

        /**
         * @brief Nodes of the composition, in key stroke order.
         * @return Composition.
         */
        [[nodiscard]] const Composition& composition() const;

      private:
        /**
         * @brief Reports whether brackets are transformed, like Go's isBracketEnabled.
         * @return True when "[" / "]" turn into "ơ" / "ư".
         */
        [[nodiscard]] bool isBracketEnabled() const;

        /**
         * @brief Keys that do not break a word, brackets included when they are transformed.
         * @return Key list.
         */
        [[nodiscard]] const std::u32string& effectKeys() const;

        /**
         * @brief Fills the cached effect key lists, called by effectKeys on first use and whenever the
         *        bracket state changes.
         */
        void buildEffectKeys() const;

        /**
         * @brief Rules of the input method that accept a key, like Go's getApplicableRules.
         * @param key Input key (any case).
         * @return Applicable rules in definition order.
         */
        [[nodiscard]] std::vector<Rule> getApplicableRules(char32_t key) const;

        /**
         * @brief Finds the node a key would transform, like Go's findTargetByKey.
         * @param composition Composition to search.
         * @param key Input key (any case).
         * @return Target node (null when there is none) and the rule that matches it.
         */
        [[nodiscard]] TargetRule findTargetByKey(const Composition& composition, char32_t key) const;

        /**
         * @brief Transformations a key produces on a composition, like the BambooEngine method.
         * @param composition Composition the key applies to.
         * @param lowerKey Lowercase key.
         * @param isUpperCase Whether the key was uppercase.
         * @return Transformations to append.
         */
        [[nodiscard]] Composition generateTransformations(const Composition& composition, char32_t lowerKey, bool isUpperCase) const;

        /**
         * @brief Applies a key to the last syllable, like Go's newComposition.
         * @param composition Composition to extend.
         * @param key Lowercase key.
         * @param isUpperCase Whether the key was uppercase.
         * @return New composition.
         */
        [[nodiscard]] Composition newComposition(const Composition& composition, char32_t key, bool isUpperCase) const;

        /**
         * @brief Virtual horn rule of the "uwo" shortcut, like Go's applyUowShortcut.
         * @param syllable Syllable to inspect.
         * @return Virtual transformation (null when the shortcut does not apply).
         */
        [[nodiscard]] TransformationPtr applyUowShortcut(const Composition& syllable) const;

        /**
         * @brief Moves the tone of a syllable to satisfy the new state, like the Go method.
         * @param syllable Syllable after the key stroke.
         * @return Extra transformations (empty when nothing must move).
         */
        [[nodiscard]] Composition           refreshLastToneTarget(const Composition& syllable) const;

        Composition                         composition_;
        InputMethod                         inputMethod_;
        EngineFlags                         flags_;
        std::optional<W2uMode>              w2uMode_;
        std::optional<BracketTransformMode> bracketTransformMode_;
        // Cached effect key lists, see buildEffectKeys.
        mutable std::u32string effectKeysPlain_;
        mutable std::u32string effectKeysWithBrackets_;
        mutable bool           effectKeysValid_ = false;
    };

} // namespace lotus

#endif // _LOTUS_ENGINE_H_
