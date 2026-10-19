/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file engine.cpp
 * @brief Implementation of the composition based input engine.
 */
#include "engine.h"

#include "case_map.h"
#include "engine_utils.h"
#include "flattener.h"
#include "utils.h"

#include <utility>

namespace lotus {

    Engine::Engine(InputMethod inputMethod, EngineFlags flags) : inputMethod_(std::move(inputMethod)), flags_(flags) {}

    const InputMethod& Engine::inputMethod() const {
        return inputMethod_;
    }

    void Engine::setFlag(EngineFlags flag) {
        flags_           = flag;
        effectKeysValid_ = false;
    }

    EngineFlags Engine::flag() const {
        return flags_;
    }

    void Engine::setW2uMode(std::optional<W2uMode> mode) {
        w2uMode_ = mode;
    }

    void Engine::setBracketTransformMode(std::optional<BracketTransformMode> mode) {
        bracketTransformMode_ = mode;
        effectKeysValid_      = false;
    }

    bool Engine::isBracketEnabled() const {
        if (!bracketTransformMode_.has_value()) {
            // Empty optional: the mode that follows the bracket transform engine flag.
            return flags_.has(EngineFlag::BracketTransformEnabled);
        }
        if (*bracketTransformMode_ == BracketTransformMode::Everywhere) {
            return true;
        }
        return *bracketTransformMode_ == BracketTransformMode::NonStart && !composition_.empty();
    }

    void Engine::buildEffectKeys() const {
        if (effectKeysValid_) {
            return;
        }
        effectKeysPlain_.assign(inputMethod_.keys.begin(), inputMethod_.keys.end());
        effectKeysWithBrackets_ = effectKeysPlain_;
        effectKeysWithBrackets_.push_back(U'[');
        effectKeysWithBrackets_.push_back(U']');
        effectKeysWithBrackets_.push_back(U'{');
        effectKeysWithBrackets_.push_back(U'}');
        effectKeysValid_ = true;
    }

    const std::u32string& Engine::effectKeys() const {
        buildEffectKeys();
        return isBracketEnabled() ? effectKeysWithBrackets_ : effectKeysPlain_;
    }

    bool Engine::isValid(bool inputIsFullComplete) const {
        const WordSplit split = extractLastWord(composition_, effectKeys());
        return lotus::isValid(split.last, inputIsFullComplete);
    }

    std::u32string Engine::processedString(ModeSet mode) const {
        Composition part;
        if (mode.has(Mode::FullText)) {
            part = composition_;
        } else if (mode.has(Mode::PunctuationMode)) {
            part = extractLastWordWithPunctuationMarks(composition_).last;
            return flatten(part, toMask(Mode::VietnameseMode));
        } else {
            part = extractLastWord(composition_, effectKeys()).last;
        }
        return flatten(part, mode);
    }

    std::vector<Rule> Engine::getApplicableRules(char32_t key) const {
        std::vector<Rule> applicableRules;
        const char32_t    lowerKey = case_map::toLower(key);
        for (const Rule& inputRule : inputMethod_.rules) {
            if (inputRule.key == lowerKey) {
                applicableRules.push_back(inputRule);
            }
        }
        return applicableRules;
    }

    TargetRule Engine::findTargetByKey(const Composition& composition, char32_t key) const {
        return findTarget(composition, getApplicableRules(key), flags_);
    }

    bool Engine::canProcessKey(char32_t key) const {
        if (utils::canProcessKey(key, std::u32string_view(inputMethod_.keys.data(), inputMethod_.keys.size()))) {
            return true;
        }
        if (isBracketEnabled()) {
            const char32_t lowerKey = case_map::toLower(key);
            if (lowerKey == U'[' || lowerKey == U']' || lowerKey == U'{' || lowerKey == U'}') {
                return true;
            }
        }
        return false;
    }

    Composition Engine::generateTransformations(const Composition& composition, char32_t lowerKey, bool isUpperCase) const {
        Composition transformations = lotus::generateTransformations(composition, getApplicableRules(lowerKey), flags_, lowerKey, isUpperCase);
        if (transformations.empty()) {
            // No applicable rule can be applied, so the key falls back to an APPENDING transformation.
            transformations = generateFallbackTransformations(getApplicableRules(lowerKey), lowerKey, isUpperCase);
        }

        if (isBracketEnabled()) {
            if (!composition.empty()) {
                const TransformationPtr& lastTrans    = composition.back();
                const bool               openStroke   = lowerKey == U'[' || lowerKey == U'{';
                const bool               closeStroke  = lowerKey == U']' || lowerKey == U'}';
                const bool               openResult   = lastTrans->rule.result == U'ơ' || lastTrans->rule.result == U'Ơ';
                const bool               strokeResult = lastTrans->rule.result == U'ư' || lastTrans->rule.result == U'Ư';
                const bool               openBracket  = openStroke && (lastTrans->rule.key == U'[' || lastTrans->rule.key == U'{') && openResult;
                const bool               closeBracket = closeStroke && (lastTrans->rule.key == U']' || lastTrans->rule.key == U'}') && strokeResult;
                if (openBracket || closeBracket) {
                    auto trans             = std::make_shared<Transformation>();
                    trans->target          = lastTrans;
                    trans->rule.effectType = EffectType::MarkTransformation;
                    trans->rule.setMark(Mark::Raw);
                    return Composition{trans};
                }
            }
            if ((lowerKey == U'[' || lowerKey == U'{') && !transformations.empty() && (transformations[0]->rule.result == U'[' || transformations[0]->rule.result == U'{')) {
                transformations[0]->rule.result   = U'ơ';
                transformations[0]->rule.effectOn = U'ơ';
                if (lowerKey == U'{') {
                    transformations[0]->isUpperCase = true;
                }
            }
            if ((lowerKey == U']' || lowerKey == U'}') && !transformations.empty() && (transformations[0]->rule.result == U']' || transformations[0]->rule.result == U'}')) {
                transformations[0]->rule.result   = U'ư';
                transformations[0]->rule.effectOn = U'ư';
                if (lowerKey == U'}') {
                    transformations[0]->isUpperCase = true;
                }
            }
        }

        // Keep old W2U logic for backward compatibility.
        const bool canApplyW2U =
            !w2uMode_.has_value() ? flags_.has(EngineFlag::W2uEnabled) : (*w2uMode_ == W2uMode::Everywhere || (*w2uMode_ == W2uMode::NonStart && !composition.empty()));

        if (canApplyW2U && lowerKey == U'w' && !transformations.empty()) {
            switch (transformations[0]->rule.result) {
                case U'w':
                    transformations[0]->rule.result   = U'ư';
                    transformations[0]->rule.effectOn = U'ư';
                    break;
                case U'W':
                    transformations[0]->rule.result   = U'Ư';
                    transformations[0]->rule.effectOn = U'Ư';
                    break;
                default: break;
            }
        }

        Composition newComposition = composition;
        newComposition.insert(newComposition.end(), transformations.begin(), transformations.end());

        // The "uwo" typing shortcut is a virtual Mark::Horn rule that targets an existing node.
        if (TransformationPtr virtualTrans = applyUowShortcut(newComposition)) {
            transformations.push_back(virtualTrans);
        }

        // Sometimes the position of a tone of a previous state must move to fit the new state:
        // chuyr -> chuỷ, then chuyrene -> chuyển.
        Composition refreshSource = composition;
        refreshSource.insert(refreshSource.end(), transformations.begin(), transformations.end());
        const Composition refreshed = this->refreshLastToneTarget(refreshSource);
        transformations.insert(transformations.end(), refreshed.begin(), refreshed.end());
        return transformations;
    }

    Composition Engine::newComposition(const Composition& composition, char32_t key, bool isUpperCase) const {
        const WordSplit   split    = extractLastSyllable(composition);
        Composition       syllable = split.last;
        const Composition produced = generateTransformations(syllable, key, isUpperCase);
        syllable.insert(syllable.end(), produced.begin(), produced.end());

        Composition result = split.previous;
        result.insert(result.end(), syllable.begin(), syllable.end());
        return result;
    }

    TransformationPtr Engine::applyUowShortcut(const Composition& syllable) const {
        const std::u32string str = flatten(syllable, Mode::ToneLess | Mode::LowerCase);
        if (!inputMethod_.superKeys.empty() && matchesUOhTail(str)) {
            const TargetRule found = findTargetByKey(syllable, inputMethod_.superKeys[0]);
            if (found.target != nullptr) {
                auto virtualTrans      = std::make_shared<Transformation>();
                virtualTrans->rule     = found.rule;
                virtualTrans->rule.key = 0; // A virtual rule must not appear in the raw string.
                virtualTrans->target   = found.target;
                return virtualTrans;
            }
        }
        return nullptr;
    }

    Composition Engine::refreshLastToneTarget(const Composition& syllable) const {
        if (flags_.has(EngineFlag::FreeToneMarking) && lotus::isValid(syllable, false)) {
            return lotus::refreshLastToneTarget(syllable, flags_.has(EngineFlag::StdToneStyle));
        }
        return Composition{};
    }

    void Engine::processString(std::u32string_view text, ModeSet mode) {
        for (const char32_t key : text) {
            processKey(key, mode);
        }
    }

    void Engine::processKey(char32_t key, ModeSet mode) {
        const char32_t lowerKey    = case_map::toLower(key);
        const bool     isUpperCase = case_map::isUpper(key);
        if (mode.has(Mode::EnglishMode) || !canProcessKey(lowerKey)) {
            if (mode.has(Mode::InReverseOrder)) {
                composition_.insert(composition_.begin(), makeAppendingTrans(lowerKey, isUpperCase));
                return;
            }
            composition_.push_back(makeAppendingTrans(lowerKey, isUpperCase));
            return;
        }
        composition_ = newComposition(composition_, lowerKey, isUpperCase);
    }

    void Engine::restoreLastWord(bool toVietnamese) {
        // Go splits the last word with the raw input method keys here, so the brackets are not part
        // of the word, unlike in isValid / processedString / canProcessKey.
        const WordSplit    split    = extractLastWord(composition_, std::u32string_view(inputMethod_.keys.data(), inputMethod_.keys.size()));
        const Composition& previous = split.previous;
        const Composition& lastComb = split.last;
        if (lastComb.empty()) {
            return;
        }
        if (!toVietnamese) {
            Composition       result = previous;
            const Composition broken = breakComposition(lastComb);
            result.insert(result.end(), broken.begin(), broken.end());
            composition_ = result;
        } else {
            Composition newComp;
            for (const TransformationPtr& tnx : lastComb) {
                newComp = newComposition(newComp, tnx->rule.key, tnx->isUpperCase);
            }
            Composition result = previous;
            result.insert(result.end(), newComp.begin(), newComp.end());
            composition_ = result;
        }
    }

    void Engine::reset() {
        composition_.clear();
    }

    void Engine::removeLastChar(bool refreshLastToneTarget) {
        const TransformationPtr lastAppending = findLastAppendingTrans(composition_);
        if (lastAppending == nullptr) {
            return;
        }
        if (!canProcessKey(lastAppending->rule.key)) {
            composition_.pop_back();
            return;
        }
        // Go also splits the last word with the raw input method keys here.
        const WordSplit    split    = extractLastWord(composition_, std::u32string_view(inputMethod_.keys.data(), inputMethod_.keys.size()));
        const Composition& previous = split.previous;
        const Composition& lastComb = split.last;
        Composition        newComb;
        for (const TransformationPtr& t : lastComb) {
            if (isSame(t->target, lastAppending) || isSame(t, lastAppending)) {
                continue;
            }
            newComb.push_back(t);
        }
        if (refreshLastToneTarget) {
            const Composition refreshed = this->refreshLastToneTarget(newComb);
            newComb.insert(newComb.end(), refreshed.begin(), refreshed.end());
        }
        Composition result = previous;
        result.insert(result.end(), newComb.begin(), newComb.end());
        composition_ = result;
    }

    const Composition& Engine::composition() const {
        return composition_;
    }

} // namespace lotus
