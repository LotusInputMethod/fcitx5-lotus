/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-state.h"
#include "common-fd-guard.h"
#include "lotus-engine.h"
#include "lotus-candidates.h"
#include "lotus-fcitx-text.h"
#include "lotus-key-pacer.h"
#include "lotus-utils.h"

#include "input_method.h"
#include "utf8.h"

#include <linux/input-event-codes.h>

#include <cstddef>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterface.h>

#include <algorithm>
#include <format>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>

namespace fcitx {
    constexpr int MAX_SCAN_LENGTH = 15;

    constexpr int kMinDelayMs = 1;
    constexpr int kMaxDelayMs = 1000;

    int           clampDelay(int value) {
        return std::clamp(value, kMinDelayMs, kMaxDelayMs);
    }

    bool isWordBreak(uint32_t ucs4) {
        // Space, tab, newline, carriage return, null, or punctuation/symbols (: ; < = > ? @)
        return ucs4 == ' ' || ucs4 == '\t' || ucs4 == '\n' || ucs4 == '\r' || ucs4 == 0 || (ucs4 >= 58 && ucs4 <= 64);
    }

    LotusState::LotusState(LotusEngine* engine, InputContext* ic) : engine_(*engine), ic_(*ic) {
        setEngine();
    }

    LotusState::~LotusState() {
        KeyPacer::instance().clearBarrierOwner(this);
    }

    void LotusState::setEngine() {
        lotusEngine_ = nullptr;

        if (engine_.config().inputMethod.value() == lotus::InputMethodId::Custom) {
            std::unordered_map<std::string, std::string> definition;
            for (const auto& keymap : *engine_.customKeymap().customKeymap) {
                definition.emplace(*keymap.key, *keymap.value);
            }
            // Go's NewCustomEngine parsed the custom definition and left spell checking off.
            lotusEngine_ = std::make_shared<lotus::KeyProcessor>(lotus::parseInputMethod(definition), false);
        } else {
            lotusEngine_ = std::make_shared<lotus::KeyProcessor>(lotus::cachedInputMethod(engine_.config().inputMethod.value()), true);
        }
        lotusEngine_->setMacroTable(engine_.macroTable());
        lotusEngine_->setDictionary(engine_.dictionary());
        setOption();
        resetMacroSkip();
    }

    void LotusState::setOption() {
        if (!lotusEngine_)
            return;
        lotus::Options option = {
            .autoNonVnRestore    = *engine_.config().autoNonVnRestore,
            .ddFreeStyle         = *engine_.config().ddFreeStyle,
            .macroEnabled        = *engine_.config().enableMacro,
            .autoCapitalizeMacro = *engine_.config().capitalizeMacro,
            .spellCheckWithDicts = *engine_.config().spellCheck,
            .modernStyle         = *engine_.config().modernStyle,
            .freeMarking         = *engine_.config().freeMarking,
            .outputCharset       = *engine_.config().outputCharset,
            .w2u                 = *engine_.config().w2u,
            .bracketTransform    = *engine_.config().bracketTransform,
            .timeFormat          = *engine_.config().timeFormat,
            .dateFormat          = *engine_.config().dateFormat,
        };

        lotusEngine_->setOptions(option);
    }

    bool LotusState::ensureUinputConnected() {
        return KeyPacer::instance().ensureConnected();
    }

    void LotusState::enqueueBackspace(size_t count, uint32_t pre_delay, uint32_t interval, uint32_t post_delay, std::string text) {
        if (count == 0)
            return;
        KeyPacer::instance().push(
            buildBackspaceJob(count, std::chrono::milliseconds{pre_delay}, std::chrono::milliseconds{interval}, std::chrono::milliseconds{post_delay}, std::move(text)));
    }

    void LotusState::enqueueSelect(size_t count, uint32_t pre_delay, uint32_t interval, uint32_t post_delay, std::string text) {
        if (count == 0)
            return;
        KeyPacer::instance().push(
            buildSelectJob(count, std::chrono::milliseconds{pre_delay}, std::chrono::milliseconds{interval}, std::chrono::milliseconds{post_delay}, std::move(text)));
    }

    bool LotusState::isAutofillCertain(const SurroundingText& s) {
        if (!s.isValid() || oldPreBuffer_.empty()) {
            return false;
        }

        auto               textRange = fcitx::utf8::MakeUTF8CharRange(s.text());
        std::u32string     u32Text(textRange.begin(), textRange.end());

        const unsigned int cursor  = s.cursor();
        const unsigned int anchor  = s.anchor();
        const size_t       textLen = u32Text.length();

        // Fix that surrounding text is delay update
        const size_t buffLen    = oldPreBuffer_.length();
        const size_t pb         = u32Text.find(oldPreBuffer_);
        size_t       rangeStart = static_cast<size_t>(cursor) >= buffLen ? static_cast<size_t>(cursor) - buffLen : 0;
        const bool   sameprefix = pb != std::u32string::npos && pb >= rangeStart && pb <= static_cast<size_t>(cursor);

        // Detect browser autofill/autocomplete suggestions via selection.
        if (cursor != anchor) {
            unsigned int selectionStart = std::min(anchor, cursor);
            unsigned int selectionEnd   = std::max(anchor, cursor);

            // Only consider it browser autofill if the selection starts at the cursor
            // and extends to the end of the line (common address bar behavior).
            if (selectionStart >= cursor || (selectionStart < cursor && selectionEnd > cursor)) {
                if (!sameprefix)
                    return false;
                // If the selection contains a newline, it's likely a multiline editor (AI ghost text),
                // not a single-line URL/Search bar.
                size_t p = u32Text.find(U'\n', selectionStart);
                return p == std::u32string::npos || p >= static_cast<size_t>(selectionEnd);
            }
        }

        if (textLen == static_cast<size_t>(cursor)) {
            realtextLen.store(textLen, std::memory_order_release);
            return false;
        }

        // Heuristic: rapid text growth in a single-line context.
        // Applied only when no newline is present after the cursor to distinguish from AI text in editors.
        // Gecko/Firefox: if buffLen > textLen, surrounding text is stale (async update race)
        if (buffLen > textLen) {
            return false;
        }
        if (textLen > static_cast<size_t>(cursor) + 1 && cursor == realtextLen.load(std::memory_order_acquire) && u32Text.find(U'\n', cursor) == std::u32string::npos && sameprefix)
            return true;

        for (auto v = realtextLen.load(std::memory_order_acquire); v < cursor && !realtextLen.compare_exchange_weak(v, cursor, std::memory_order_acq_rel);)
            ;
        return false;
    }

    void LotusState::handlePreeditMode(KeyEvent& keyEvent, KeySym currentSym) {
        if (lotusEngine_->processKey(engineKeyValue(currentSym), lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states()))))
            keyEvent.filterAndAccept();
        const std::u32string commitText = pullCommitString();
        lotus::fcitx_text::commit(&ic_, commitText, last_commit_time_);
        ic_.inputPanel().reset();
        const std::u32string preeditText = pullPreeditString();
        if (!preeditText.empty()) {
            Text text = lotus::fcitx_text::toText(preeditText);
            if (ic_.capabilityFlags().test(CapabilityFlag::Preedit))
                ic_.inputPanel().setClientPreedit(text);
            else
                ic_.inputPanel().setPreedit(text);
        }
        ic_.updatePreedit();
        ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void LotusState::updateEmojiPageStatus(CommonCandidateList* commonList) {
        if ((commonList == nullptr) || commonList->empty()) {
            return;
        }

        int pageSize = commonList->pageSize();
        if (pageSize <= 0) {
            pageSize = 9;
        }

        int         totalItems  = commonList->totalSize();
        int         currentPage = commonList->currentPage() + 1;
        int         totalPages  = (totalItems + pageSize - 1) / pageSize;

        std::string status = _("Page ") + std::to_string(currentPage) + "/" + std::to_string(totalPages);
        ic_.inputPanel().setAuxDown(Text(status));
    }

    void LotusState::handleEmojiMode(KeyEvent& keyEvent) {
        const KeySym currentSym      = keyEvent.rawKey().sym();
        bool         isCtrlBackspace = isBackspace(currentSym) && ((keyEvent.rawKey().states() & KeyState::Ctrl) != 0U);

        if (keyEvent.key().hasModifier() && !isCtrlBackspace) {
            keyEvent.forward();
            return;
        }

        auto baseList   = ic_.inputPanel().candidateList();
        auto commonList = std::dynamic_pointer_cast<CommonCandidateList>(baseList);
        if (commonList && currentSym >= FcitxKey_1 && currentSym <= FcitxKey_9) {
            int offset      = currentSym - FcitxKey_1;
            int globalIndex = (commonList->currentPage() * commonList->pageSize()) + offset;

            if (globalIndex < commonList->totalSize()) {
                commonList->candidateFromAll(globalIndex).select(&ic_);
                keyEvent.filterAndAccept();
                return;
            }
        }

        if (commonList && !commonList->empty()) {
            int  globalCursorIndex = commonList->globalCursorIndex();
            int  totalSize         = commonList->totalSize();
            int  currentPage       = commonList->currentPage();
            int  pageSize          = commonList->pageSize();
            int  localCursorIndex  = globalCursorIndex - (currentPage * pageSize);

            bool handled = false;

            switch (currentSym) {
                case FcitxKey_Tab:
                case FcitxKey_Down: {
                    if (localCursorIndex < pageSize - 1 && globalCursorIndex < totalSize - 1) {
                        commonList->setGlobalCursorIndex(globalCursorIndex + 1);
                    } else {
                        commonList->setGlobalCursorIndex(currentPage * pageSize);
                    }
                    handled = true;
                    break;
                }

                case FcitxKey_ISO_Left_Tab:
                case FcitxKey_Up: {
                    if (localCursorIndex > 0) {
                        commonList->setGlobalCursorIndex(globalCursorIndex - 1);
                    } else {
                        int lastIndex = std::min((currentPage * pageSize) + pageSize - 1, totalSize - 1);
                        commonList->setGlobalCursorIndex(lastIndex);
                    }
                    handled = true;
                    break;
                }
                case FcitxKey_Page_Down:
                case FcitxKey_Right: {
                    if (commonList->hasNext()) {
                        commonList->next();
                        int newPage = commonList->currentPage();
                        commonList->setGlobalCursorIndex(newPage * pageSize);
                        handled = true;
                    }
                    break;
                }
                case FcitxKey_Page_Up:
                case FcitxKey_Left: {
                    if (commonList->hasPrev()) {
                        commonList->prev();
                        int newPage = commonList->currentPage();
                        commonList->setGlobalCursorIndex(newPage * pageSize);
                        handled = true;
                    }
                    break;
                }
                default: break;
            }

            if (handled) {
                updateEmojiPageStatus(commonList.get());
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }
        }

        if (isBackspace(currentSym)) {
            if (!emojiBuffer_.empty()) {
                if (isCtrlBackspace) {
                    emojiBuffer_.clear();
                } else {
                    emojiBuffer_.pop_back();
                }
                keyEvent.filterAndAccept();
            } else {
                keyEvent.forward();
            }
            updateEmojiPreedit();
            return;
        }

        switch (currentSym) {
            case FcitxKey_space:
            case FcitxKey_Return: {
                if (commonList && !commonList->empty()) {
                    int globalIdx = commonList->globalCursorIndex();
                    commonList->candidateFromAll(globalIdx).select(&ic_);
                    keyEvent.filterAndAccept();
                } else if (currentSym == FcitxKey_Return && !emojiBuffer_.empty()) {
                    lotus::fcitx_text::commit(&ic_, emojiBuffer_, last_commit_time_);
                    emojiBuffer_.clear();
                    updateEmojiPreedit();
                    keyEvent.filterAndAccept();
                } else {
                    keyEvent.forward();
                }
                return;
            }

            case FcitxKey_Escape: {
                emojiBuffer_.clear();
                emojiCandidates_.clear();
                ic_.inputPanel().reset();
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }

            default: break;
        }

        {
            const uint32_t codePoint = Key::keySymToUnicode(currentSym);
            if (codePoint != 0U) {
                emojiBuffer_.push_back(static_cast<char32_t>(codePoint));
                keyEvent.filterAndAccept();
                updateEmojiPreedit();
            } else {
                keyEvent.forward();
            }
        }
    }
    void LotusState::updateEmojiPreedit() {
        if (emojiBuffer_.empty()) {
            emojiCandidates_ = engine_.emojiLoader().history();
            if (emojiCandidates_.empty()) {
                ic_.inputPanel().reset();
                ic_.updatePreedit();
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
        } else {
            emojiCandidates_ = engine_.emojiLoader().search(lotus::utf8::encode(emojiBuffer_));
        }

        if (!emojiBuffer_.empty()) {
            Text preeditText;
            preeditText.append(lotus::utf8::encode(emojiBuffer_), TextFormatFlag::Underline);
            preeditText.setCursor(static_cast<int>(preeditText.textLength()));
            if (ic_.capabilityFlags().test(CapabilityFlag::Preedit))
                ic_.inputPanel().setClientPreedit(preeditText);
            else
                ic_.inputPanel().setPreedit(preeditText);
        } else {
            ic_.inputPanel().setClientPreedit(Text());
            ic_.inputPanel().setPreedit(Text());
        }

        if (!emojiCandidates_.empty()) {
            auto candidateList = std::make_unique<CommonCandidateList>();
            candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
            candidateList->setPageSize(9);

            for (size_t i = 0; i < emojiCandidates_.size(); ++i) {
                size_t localIndex = (i % 9) + 1;
                Text   displayLabel;
                if (emojiBuffer_.empty()) {
                    displayLabel.append(std::to_string(localIndex) + ": " + emojiCandidates_[i].output, TextFormatFlag::NoFlag);
                } else {
                    displayLabel.append(std::to_string(localIndex) + ": " + emojiCandidates_[i].trigger + " " + emojiCandidates_[i].output, TextFormatFlag::NoFlag);
                }
                candidateList->append(std::make_unique<EmojiCandidateWord>(displayLabel, this, emojiCandidates_[i]));
            }
            candidateList->setGlobalCursorIndex(0);

            ic_.inputPanel().setCandidateList(std::move(candidateList));
            auto currentList = std::dynamic_pointer_cast<CommonCandidateList>(ic_.inputPanel().candidateList());
            updateEmojiPageStatus(currentList.get());
        } else {
            ic_.inputPanel().setCandidateList(nullptr);
        }

        ic_.updatePreedit();
        ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void LotusState::commitPendingReplacement() {
        // Validate surr cursor pos should match realtextLen after all BS/Left applied
        const auto& surr = ic_.surroundingText();
        if (surr.isValid() && surr.cursor() == realtextLen.load(std::memory_order_acquire)) {
            LOTUS_INFO("Skip retry");
        } else {
            // Retry x3 (2 ms each), khi can (chromium,electron,...)
            for (int retry = 0; retry < 3; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                const auto& surr2 = ic_.surroundingText();
                if (surr2.isValid() && surr2.cursor() == realtextLen.load(std::memory_order_acquire)) {
                    break;
                }
            }
        }
        std::u32string commitText = std::move(pending_commit_string_);
        pending_commit_string_.clear();

        if (!commitText.empty()) {
            lotus::fcitx_text::commit(&ic_, commitText, last_commit_time_);
            LOTUS_INFO("Commit: " + lotus::utf8::encode(commitText));
        }
    }

    uint32_t LotusState::echoSequence() const {
        return static_cast<uint32_t>(current_backspace_count_);
    }

    void LotusState::commitFromChain(const std::string& text) {
        // The chain text is authoritative (same value performReplacement stored,
        // re-stated here so aborts/concurrency can't substitute a stale string).
        // The chain carries UTF-8 (fcitx boundary); the pending string is UTF-32.
        pending_commit_string_ = lotus::utf8::decode(text);
        commitPendingReplacement();
        expected_backspaces_     = 0;
        current_backspace_count_ = 0;
        is_deleting_.store(false, std::memory_order_release);
        replayBufferedKeys();
    }

    bool LotusState::handleUInputKeyPress(KeyEvent& event, KeySym currentSym) {
        if (!is_deleting_.load()) {
            return false;
        }
        // In Select mode the uinput server emits Shift+Left, so the echoed Left
        // presses are the selection signal. The last selection echo is
        // swallowed here with the commit held for the chain barrier
        // (AwaitEcho + CommitPending): the Delete branch below notifies the
        // pacer, which resumes and fires the commit. The app only ever sees
        // the count it should select.
        const bool isSelectEcho = realMode == LotusMode::Select && currentSym == FcitxKey_Left;
        if (!isBackspace(currentSym) && !isSelectEcho) {
            return false;
        }
        current_backspace_count_ += 1;
        // Decide swallowing BEFORE notifyEcho: the barrier may fire the
        // commit synchronously, and the commit replays buffered keys into a
        // nested replacement that resets these very counters for the NEW
        // job. The echo must be judged against the job it belongs to, not
        // the nested one.
        const bool lastEcho = current_backspace_count_ >= expected_backspaces_;
        KeyPacer::instance().notifyEcho(current_backspace_count_);
        if (!lastEcho) {
            return false; // Allow intermediate backspaces/lefts to reach the app to clear autofill/old text / extend the selection.
        }
        // Both tails end the same way: the last echo (selection Left in
        // Select, backspace in Smooth) is swallowed — the app must not
        // receive it. The commit has already fired synchronously inside
        // notifyEcho() above through the parked chain barrier
        // (AwaitEcho -> Commit -> commitFromChain), which also cleared
        // is_deleting_ and replayed buffered keys.
        event.filterAndAccept();
        return true;
    }

    void LotusState::performReplacement(DiffPart diffPart) {
        const int  commitMs = clampDelay(appRuleSetting_.commitInterval > 0 ? appRuleSetting_.commitInterval : *engine_.config().defaultCommitInterval);
        const auto interval =
            static_cast<uint32_t>(clampDelay(appRuleSetting_.backspaceInterval > 0 ? appRuleSetting_.backspaceInterval : *engine_.config().defaultBackspaceInterval));
        uint32_t pre_delay = 0;
        if (last_commit_time_.time_since_epoch().count() > 0) {
            auto now       = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_commit_time_).count();
            if (elapsedMs < commitMs) {
                pre_delay = static_cast<uint32_t>(commitMs - elapsedMs);
            }
        }
        LOTUS_INFO("Perform replacement: " + lotus::utf8::encode(diffPart.deletedPart) + " -> " + lotus::utf8::encode(diffPart.addedPart)); //NOLINT
        // A new job replaces any in-flight one: drop the old queue first, or
        // its stale barrier/commit could fire after the overwrite. No-op when
        // the queue already drained (the normal case).
        KeyPacer::instance().clear();
        KeyPacer::instance().setBarrierOwner(this);
        current_backspace_count_       = 0;
        pending_commit_string_         = diffPart.addedPart;
        expected_backspaces_           = diffPart.deletedPart.size();
        const auto& surrounding        = ic_.surroundingText();
        bool        isAutofillCertain_ = isAutofillCertain(surrounding);
        if (realMode != LotusMode::Minecraft) {
            ++expected_backspaces_;
            // Enable Autofill detection for all frontends (Wayland/IBus).
            // This fixes the "toôi" duplication bug in Chromium-based search bars.
            // The isAutofillCertain function has been optimized to differentiate
            // between browser autofill and AI ghost text.
            // Skipped for Select: the app only sees expected_backspaces_ - 1
            // arrows, and the autofill suffix is already selected, so the one
            // arrow reaches the character being replaced and the commit swaps
            // both at once.
            if (realMode != LotusMode::SuperSmooth && isAutofillCertain_) {
                ++expected_backspaces_;
            }
        }
        is_deleting_.store(true, std::memory_order_release);
        const auto post_delay = static_cast<uint32_t>(clampDelay(appRuleSetting_.postDelay > 0 ? appRuleSetting_.postDelay : *engine_.config().defaultPostDelay));
        // Select mode: the uinput server selects with Shift+Left instead. The
        // trigger-key compensation above still applies because the last echoed
        // Left is swallowed; autofill compensation does not, see above.
        if (realMode == LotusMode::Select && !isAutofillCertain_) {
            enqueueSelect(expected_backspaces_, pre_delay, interval, post_delay, lotus::utf8::encode(diffPart.addedPart));
            LOTUS_INFO("Send select of " + std::to_string(expected_backspaces_) + " characters");
            return;
        }
        enqueueBackspace(expected_backspaces_, pre_delay, interval, post_delay, lotus::utf8::encode(diffPart.addedPart));
        LOTUS_INFO("Send " + std::to_string(expected_backspaces_) + " backspaces");
    }

    bool LotusState::checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym) {
        if (keyEvent.key().isCursorMove() || currentSym == FcitxKey_Tab || currentSym == FcitxKey_KP_Tab || currentSym == FcitxKey_ISO_Left_Tab || currentSym == FcitxKey_Escape ||
            keyEvent.key().hasModifier()) {
            is_deleting_.store(false, std::memory_order_release);
            expected_backspaces_     = 0;
            current_backspace_count_ = 0;
            KeyPacer::instance().clear();
            pending_commit_string_.clear();
            hasHistory_ = false;
            (*lotusEngine_).reset();
            oldPreBuffer_.clear();
            return true;
        }

        if (currentSym == FcitxKey_Delete) {
            return true;
        }

        if (currentSym >= FcitxKey_KP_0 && currentSym <= FcitxKey_KP_9) {
            currentSym = static_cast<KeySym>(FcitxKey_0 + (currentSym - FcitxKey_KP_0));
            return false;
        }

        switch (currentSym) {
            case FcitxKey_KP_Add: {
                currentSym = FcitxKey_plus;
                break;
            }
            case FcitxKey_KP_Subtract: {
                currentSym = FcitxKey_minus;
                break;
            }
            case FcitxKey_KP_Divide: {
                currentSym = FcitxKey_slash;
                break;
            }
            case FcitxKey_KP_Multiply: {
                currentSym = FcitxKey_asterisk;
                break;
            }
            case FcitxKey_KP_Decimal: {
                currentSym = FcitxKey_period;
                break;
            }
            case FcitxKey_KP_Enter: {
                currentSym = FcitxKey_Return;
                break;
            }
            case FcitxKey_KP_Equal: {
                currentSym = FcitxKey_equal;
                break;
            }
            case FcitxKey_KP_Space: {
                currentSym = FcitxKey_space;
                break;
            }
            default: break;
        }
        return false;
    }

    bool LotusState::forwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym) {
        if (checkForwardSpecialKey(keyEvent, currentSym)) {
            keyEvent.forward();
            return true;
        }
        return false;
    }

    void LotusState::commitOrReplace(DiffPart diffPart, const std::string& logMsg) {
        if (realMode == LotusMode::SurroundingText) {
            ic_.deleteSurroundingText(-1, 1);
            lotus::fcitx_text::commit(&ic_, diffPart.addedPart, last_commit_time_);
        } else { // Uinput, Smooth, Preedit, etc.
            performReplacement(diffPart);
        }
        LOTUS_INFO(logMsg);
    }

    std::u32string LotusState::pullCommitString() {
        return lotusEngine_->takeCommit();
    }

    std::u32string LotusState::pullPreeditString() {
        return lotusEngine_->preedit();
    }

    void LotusState::handleUinputMode(KeyEvent& keyEvent, KeySym currentSym) {
        if (forwardSpecialKey(keyEvent, currentSym))
            return;

        if (!ensureUinputConnected()) {
            // Best effort only; guards below re-check before replacing.
        }

        if (isBackspace(currentSym) || currentSym == FcitxKey_Return) {
            if (isBackspace(currentSym)) {
                hasHistory_ = true;
                lotusEngine_->processKey(FcitxKey_BackSpace, {});
                oldPreBuffer_ = pullPreeditString();
            } else {
                hasHistory_ = false;
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
            return;
        }

        const uint32_t keyCodePoint = Key::keySymToUnicode(currentSym);
        if (keyCodePoint == 0U) {
            keyEvent.forward();
            return;
        }
        const std::u32string keyText(1, static_cast<char32_t>(keyCodePoint));

        bool                 processed = lotusEngine_->processKey(engineKeyValue(currentSym), lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states())));

        const std::u32string commitStr = pullCommitString();
        if (!commitStr.empty()) {
            DiffPart diffPart = compareAndSplitU32(oldPreBuffer_, commitStr);

            if (!diffPart.deletedPart.empty()) {
                performReplacement(diffPart);
                keyEvent.filterAndAccept();
            } else {
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!diffPart.addedPart.empty() && (keyText != diffPart.addedPart || wasAutoCapitalized)) {
                    // Prevent auto-capitalized character replacement from stripping out Vietnamese chars
                    if (diffPart.addedPart.size() > 1 && diffPart.addedPart.back() == U' ') {
                        // Stripping the trigger key (space) from addedPart
                        diffPart.addedPart.remove_suffix(1);
                    }
                    lotus::fcitx_text::commit(&ic_, diffPart.addedPart, last_commit_time_);
                    keyEvent.filterAndAccept();
                } else {
                    keyEvent.forward();
                }
            }

            hasHistory_ = false;
            (*lotusEngine_).reset();
            oldPreBuffer_.clear();

            return;
        }

        if (!processed) {
            const std::u32string preeditCurrent = pullPreeditString();
            if (preeditCurrent.empty()) {
                hasHistory_ = false;
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
                keyEvent.forward();
            }
            return;
        }

        hasHistory_ = true;
        realtextLen.fetch_add(1, std::memory_order_acq_rel);

        const std::u32string preeditStr = pullPreeditString();

        DiffPart             diffPart = compareAndSplitU32(oldPreBuffer_, preeditStr);
        if (diffPart.deletedPart.empty()) {
            bool isCommit           = false;
            bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
            if (!diffPart.addedPart.empty()) {
                oldPreBuffer_ = preeditStr;
                if (wasAutoCapitalized || diffPart.addedPart != keyText) {
                    lotus::fcitx_text::commit(&ic_, diffPart.addedPart, last_commit_time_);
                    keyEvent.filterAndAccept();
                    isCommit = true;
                }
            }
            if (!isCommit) {
                keyEvent.forward();
            }
        } else {
            if (!ensureUinputConnected()) {
                LOTUS_ERROR("Cannot connect to uinput server, commit rawkey");
                std::string rawKey = keyEvent.key().toString();
                if (!rawKey.empty()) {
                    ic_.commitString(rawKey);
                }
                return;
            }

            if (is_deleting_.load()) {
                is_deleting_.store(false, std::memory_order_release);
                KeyPacer::instance().clear();
            }

            keyEvent.filterAndAccept();
            performReplacement(diffPart);
            oldPreBuffer_ = preeditStr;
        }
    }

    void LotusState::handleSurroundingText(KeyEvent& keyEvent, KeySym currentSym) {
        if (forwardSpecialKey(keyEvent, currentSym))
            return;
        auto* ic = keyEvent.inputContext();
        if ((ic == nullptr) || !ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
            LOTUS_WARN("Surrounding text not supported");
            keyEvent.forward();
            return;
        }

        const auto& surrounding = ic->surroundingText();
        if (!surrounding.isValid()) {
            LOTUS_WARN("Surrounding text is invalid");
            keyEvent.forward();
            return;
        }

        if (isBackspace(keyEvent.rawKey().sym())) {
            (*lotusEngine_).reset();
            keyEvent.forward();
            return;
        }

        const std::string& text   = surrounding.text();
        const unsigned int cursor = std::min(surrounding.anchor(), surrounding.cursor());

        const size_t       textLen = lotus::utf8::length(text);

        if (cursor <= 0 || cursor > textLen) {
            processNormalKey(keyEvent, currentSym);
            return;
        }

        {
            // The whole surrounding text is never decoded: the byte scan stops after MAX_SCAN_LENGTH
            // code points before the cursor, and only that slice is decoded for the engine.
            auto startIter = utf8::nextNChar(text.begin(), cursor);
            auto endIter   = startIter;

            int  scanCount = 0;
            while (startIter != text.begin() && scanCount < MAX_SCAN_LENGTH) {
                auto prev = startIter;
                if (prev != text.begin()) {
                    --prev;
                    while (prev != text.begin() && ((static_cast<unsigned char>(*prev) & 0xC0U) == 0x80)) {
                        --prev;
                    }
                }

                const uint32_t ucs4 = utf8::getChar(prev, text.end());

                if (isWordBreak(ucs4))
                    break;

                startIter = prev;
                ++scanCount;
            }

            const std::string oldWordText(startIter, endIter);

            if (oldWordText.empty()) {
                processNormalKey(keyEvent, currentSym);
                return;
            }

            const std::u32string oldWord = lotus::utf8::decode(oldWordText);

            lotusEngine_->rebuildFromText(oldWord);

            bool processed = lotusEngine_->processKey(engineKeyValue(currentSym), lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states())));

            if (!processed) {
                keyEvent.forward();
                (*lotusEngine_).reset();
                return;
            }

            std::u32string newWord = pullCommitString();
            newWord += pullPreeditString();

            DiffPart diffPart = compareAndSplitU32(oldWord, newWord);
            if ((diffPart.deletedPart.empty() || diffPart.deletedPart == oldWord) && diffPart.addedPart == lotus::utf8::decode(keyEvent.key().toString())) {
                (*lotusEngine_).reset();
                keyEvent.forward();
                return;
            }

            if (!diffPart.deletedPart.empty() || !diffPart.addedPart.empty()) {
                const size_t charsToDelete = diffPart.deletedPart.size();

                if (charsToDelete > 0) {
                    ic->deleteSurroundingText(-static_cast<int>(charsToDelete), static_cast<int>(charsToDelete));
                }

                if (!diffPart.addedPart.empty()) {
                    lotus::fcitx_text::commit(ic, diffPart.addedPart, last_commit_time_);
                }

                (*lotusEngine_).reset();
                keyEvent.filterAndAccept();
                return;
            }

            (*lotusEngine_).reset();
            keyEvent.filterAndAccept();
            return;
        }
    }

    void LotusState::processNormalKey(KeyEvent& keyEvent, KeySym currentSym) {
        auto* ic = keyEvent.inputContext();
        (*lotusEngine_).reset();
        bool processed = lotusEngine_->processKey(engineKeyValue(currentSym), lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states())));
        if (processed) {
            std::u32string out = pullCommitString();
            out += pullPreeditString();

            if (!out.empty()) {
                lotus::fcitx_text::commit(ic, out, last_commit_time_);
            }

            (*lotusEngine_).reset();
            keyEvent.filterAndAccept();
        } else {
            keyEvent.forward();
        }
    }

    void LotusState::handleDoubleSpaceReplacement() {
        commitOrReplace({.deletedPart = U" ", .addedPart = U". "}, "Commit: . ");
        if (*engine_.config().autoCapitalizeAfterPunctuation) {
            isPrevPunctuation_ = true;
            shouldCapitalize_  = true;
        }
    }

    void LotusState::handleDoubleHyphenReplacement() {
        commitOrReplace({.deletedPart = U"-", .addedPart = U"—"}, "Commit: — (em-dash)");
    }

    void LotusState::handleOffModeMacro(KeyEvent& keyEvent, KeySym currentSym) {
        if (forwardSpecialKey(keyEvent, currentSym))
            return;

        if (!ensureUinputConnected()) {
            // Best effort only; macro path below falls back when offline.
        }

        if (isBackspace(currentSym)) {
            lotusEngine_->processKey(FcitxKey_BackSpace, {});
            oldPreBuffer_ = pullPreeditString();
            keyEvent.forward();
            return;
        }

        if (currentSym == FcitxKey_Return) {
            if (!oldPreBuffer_.empty()) {
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
            return;
        }

        const uint32_t       keyCodePoint = Key::keySymToUnicode(currentSym);
        const std::u32string keyText      = keyCodePoint != 0U ? std::u32string(1, static_cast<char32_t>(keyCodePoint)) : std::u32string{};

        bool                 processed = lotusEngine_->processKey(engineKeyValue(currentSym), lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states())));

        const std::u32string commitStr = pullCommitString();
        if (processed && !commitStr.empty()) {

            // Determine if this is a macro expansion or just confirmed typed text
            bool isMacroExpansion = false;
            if (keyText.empty()) {
                isMacroExpansion = (commitStr != oldPreBuffer_);
            } else {
                isMacroExpansion = (commitStr != oldPreBuffer_ + keyText);
            }

            if (isMacroExpansion) {
                LOTUS_INFO("Macro expansion: '" + lotus::utf8::encode(oldPreBuffer_) + "' -> '" + lotus::utf8::encode(commitStr) + "'");
                // Try uinput replacement first, fallback to deleteSurroundingText, then plain commit
                if (ensureUinputConnected() && !oldPreBuffer_.empty()) {
                    performReplacement({.deletedPart = oldPreBuffer_, .addedPart = commitStr});
                } else if (ic_.capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                    const auto& surrounding = ic_.surroundingText();
                    if (surrounding.isValid()) {
                        const size_t oldLen = oldPreBuffer_.size();
                        if (oldLen > 0) {
                            ic_.deleteSurroundingText(-static_cast<int>(oldLen), static_cast<int>(oldLen));
                        }
                        lotus::fcitx_text::commit(&ic_, commitStr, last_commit_time_);
                    } else {
                        lotus::fcitx_text::commit(&ic_, commitStr, last_commit_time_);
                    }
                } else {
                    lotus::fcitx_text::commit(&ic_, commitStr, last_commit_time_);
                }
                keyEvent.filterAndAccept();
            } else {
                // No macro: typed text confirmed by engine, just forward trigger key
                keyEvent.forward();
            }

            oldPreBuffer_.clear();
            hasHistory_ = false;
            return;
        }

        // 8. No commit or engine rejected the key
        if (processed || !commitStr.empty()) {
            // Engine processed the key (building shadow state)
            // OR engine rejected the key but committed old text (non-processable key)
            oldPreBuffer_ = pullPreeditString();
            if (!processed) {
                // Engine committed old text but didn't process the new key → forward the key
                oldPreBuffer_.clear();
                hasHistory_ = false;
            }
            keyEvent.forward();
        } else {
            // Engine didn't handle this key
            if (!oldPreBuffer_.empty()) {
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
        }
    }

    bool LotusState::handleKeyEventGuards(KeyEvent& keyEvent) {
        if (!lotusEngine_)
            return true;
        if (realMode == LotusMode::Preedit) {
            if (keyEvent.rawKey().check(FcitxKey_Shift_L) || keyEvent.rawKey().check(FcitxKey_Shift_R))
                return true;
        } else {
            if (keyEvent.rawKey().isModifier() && !is_deleting_.load(std::memory_order_acquire)) {
                handleModifierTap(keyEvent);
                return true;
            }
            cancelModifierTap();
        }
        if (keyEvent.isRelease())
            return true;
        return false;
    }

    void LotusState::keyEvent(KeyEvent& keyEvent) {
        if (handleKeyEventGuards(keyEvent))
            return;
        if (!ensureUinputConnected()) {
            LOTUS_WARN("Cannot connect to uinput server, replacements will fall back");
        }
        // A parked echo wait is legitimate in-flight state (commit held for
        // echoes), not stale: only abort when nothing is awaited. A queued
        // (not yet parked) barrier counts as awaited too: its echoes may
        // already be arriving (e.g. the Delete echo landing during the
        // post-Delete sleep), and clearing the queue here would drop the
        // pending AwaitEcho+Commit so the commit never fires.
        if (!KeyPacer::instance().isWaiting() && !KeyPacer::instance().hasPendingBarrierFor(this) && current_backspace_count_ >= expected_backspaces_ && is_deleting_.load()) {
            LOTUS_WARN(std::format("Aborting in-flight replacement: count={} expected={} pending={}", current_backspace_count_, expected_backspaces_,
                                   lotus::utf8::encode(pending_commit_string_)));
            is_deleting_.store(false);
            current_backspace_count_ = 0;
            expected_backspaces_     = 0;
            KeyPacer::instance().clear();
            if (!buffered_keys_.empty()) {
                replayBufferedKeys();
            }
        }

        if (g_mouse_clicked.load(std::memory_order_acquire) && !is_deleting_.load(std::memory_order_acquire)) {
            g_mouse_clicked.store(false, std::memory_order_release);
            clearAllBuffers();
        }
        KeySym currentSym = keyEvent.rawKey().sym();
        applyAutoCapitalize(keyEvent, currentSym);

        if (handleKeyWhileDeleting(keyEvent, currentSym))
            return;

        if (handleDoubleSpaceToPeriod(keyEvent, currentSym))
            return;

        if (handleDoubleHyphenToEmDash(keyEvent, currentSym))
            return;

        handleActiveMode(keyEvent, currentSym);
        reEnableMacroAfterWordEnd();
    }

    void LotusState::ensureUinputConnection() {
        ensureUinputConnected();
    }

    void LotusState::completePendingReplacement() {
        // Same EIS-aware stale check as keyEvent: parked echo wait is in-flight.
        if (!KeyPacer::instance().isWaiting() && current_backspace_count_ >= expected_backspaces_ && is_deleting_.load()) {
            is_deleting_.store(false);
            current_backspace_count_ = 0;
            expected_backspaces_     = 0;
            KeyPacer::instance().clear();
            if (!buffered_keys_.empty()) {
                replayBufferedKeys();
            }
        }
    }

    void LotusState::applyAutoCapitalize(KeyEvent& keyEvent, KeySym& currentSym) {
        if (*engine_.config().autoCapitalizeAfterPunctuation && realMode != LotusMode::Off) {
            // Ignore auto-capitalize side-effects if we're processing automated replacement backspaces
            bool isAutomatedBackspace = is_deleting_.load(std::memory_order_acquire) && isBackspace(currentSym);

            if (!isAutomatedBackspace) {
                if (shouldCapitalize_) {
                    if (currentSym >= FcitxKey_a && currentSym <= FcitxKey_z) {
                        auto upperSym = static_cast<KeySym>(currentSym - (FcitxKey_a - FcitxKey_A));
                        currentSym    = upperSym;
                        keyEvent.setKey(Key(upperSym, keyEvent.rawKey().states()));
                        shouldCapitalize_ = false;
                    } else if (currentSym != FcitxKey_space) {
                        shouldCapitalize_ = false;
                    }
                }

                switch (currentSym) {
                    case FcitxKey_period:
                    case FcitxKey_exclam:
                    case FcitxKey_question: isPrevPunctuation_ = true; break;
                    case FcitxKey_Return:
                    case FcitxKey_KP_Enter:
                        shouldCapitalize_  = true;
                        isPrevPunctuation_ = false;
                        break;
                    case FcitxKey_space:
                        if (isPrevPunctuation_) {
                            shouldCapitalize_  = true;
                            isPrevPunctuation_ = false;
                        }
                        break;
                    default:
                        if (currentSym != FcitxKey_space) {
                            isPrevPunctuation_ = false;
                        }
                        break;
                }
            }
        }
    }

    bool LotusState::handleKeyWhileDeleting(KeyEvent& keyEvent, KeySym currentSym) {
        if (is_deleting_.load(std::memory_order_acquire)) {
            if (isBackspace(currentSym)) {
                if (realtextLen.load(std::memory_order_acquire) > 0)
                    realtextLen.fetch_sub(1, std::memory_order_acq_rel);
                if (handleUInputKeyPress(keyEvent, currentSym)) {
                    return true;
                }
            } else if (realMode == LotusMode::Select && (currentSym == FcitxKey_Left || currentSym == FcitxKey_Delete)) {
                if (currentSym == FcitxKey_Left) {
                    // Echoed left arrow extends the selection in the app: consume it as
                    // a selection signal, never buffer it for replay.
                    if (realtextLen.load(std::memory_order_acquire) > 0)
                        realtextLen.fetch_sub(1, std::memory_order_acq_rel);
                    handleUInputKeyPress(keyEvent, currentSym);
                } else {
                    // Delete echo while deleting: count it (the barrier target
                    // spans the selection Lefts plus this Delete) and notify;
                    // the chain commits once the barrier lifts. Forwarded so
                    // the app deletes before the commit lands.
                    if (is_deleting_.load(std::memory_order_acquire)) {
                        current_backspace_count_ += 1;
                        KeyPacer::instance().notifyEcho(current_backspace_count_);
                    }
                    keyEvent.forward();
                }
                return true;
            } else {
                KeySym    symToBuffer   = currentSym;
                KeyStates stateToBuffer = keyEvent.rawKey().states();
                if (realMode == LotusMode::Select && current_backspace_count_ < expected_backspaces_ && stateToBuffer.test(KeyState::Shift)) {
                    symToBuffer   = unshiftKeySym(symToBuffer);
                    stateToBuffer = stateToBuffer.unset(KeyState::Shift);
                }
                std::string keyUtf8Check = Key::keySymToUTF8(symToBuffer);
                if (!keyUtf8Check.empty() && buffered_keys_.size() < MAX_BUFFERED_KEYS) {
                    LOTUS_WARN("Typing so fast, add key to queue");
                    buffered_keys_.push_back({.sym = symToBuffer, .state = stateToBuffer});
                }
                keyEvent.filterAndAccept();
            }
            return true;
        }
        return false;
    }

    bool LotusState::handleDoubleSpaceToPeriod(KeyEvent& keyEvent, KeySym currentSym) {
        if (*engine_.config().doubleSpaceToPeriod && realMode != LotusMode::Off) {
            bool isSpaceKey = (currentSym == FcitxKey_space || currentSym == FcitxKey_KP_Space);
            if (isSpaceKey && !keyEvent.key().hasModifier()) {
                if (isPrevSpace_) {
                    keyEvent.filterAndAccept();
                    handleDoubleSpaceReplacement();
                    isPrevSpace_ = false;
                    return true;
                }
                isPrevSpace_ = true;
            } else {
                isPrevSpace_ = false;
            }
        }
        return false;
    }

    bool LotusState::handleDoubleHyphenToEmDash(KeyEvent& keyEvent, KeySym currentSym) {
        if (*engine_.config().doubleHyphenToEmDash && realMode != LotusMode::Off) {
            bool isHyphenKey = (currentSym == FcitxKey_minus || currentSym == FcitxKey_KP_Subtract);
            if (isHyphenKey && !keyEvent.key().hasModifier()) {
                if (isPrevHyphen_) {
                    keyEvent.filterAndAccept();
                    handleDoubleHyphenReplacement();
                    isPrevHyphen_ = false;
                    return true;
                }
                isPrevHyphen_ = true;
            } else {
                isPrevHyphen_ = false;
            }
        }
        return false;
    }

    void LotusState::handleActiveMode(KeyEvent& keyEvent, KeySym currentSym) {
        switch (realMode) {
            case LotusMode::Smooth:
            case LotusMode::Minecraft:
            case LotusMode::SuperSmooth:
            case LotusMode::Select: {
                handleUinputMode(keyEvent, currentSym);
                break;
            }
            case LotusMode::SurroundingText: {
                handleSurroundingText(keyEvent, currentSym);
                break;
            }
            case LotusMode::Preedit: {
                handlePreeditMode(keyEvent, currentSym);
                break;
            }
            case LotusMode::Emoji: {
                handleEmojiMode(keyEvent);
                break;
            }
            default: {
                if (*engine_.config().enableMacroInOffMode && *engine_.config().enableMacro) {
                    handleOffModeMacro(keyEvent, currentSym);
                }
                break;
            }
        }
    }

    void LotusState::reset(bool isFocusOut) {
        const auto& surrounding = ic_.surroundingText();
        const auto& text        = surrounding.text();
        size_t      textLen     = utf8::length(text);
        realtextLen.store(textLen, std::memory_order_release);
        if (is_deleting_.load(std::memory_order_acquire)) {
            return;
        }
        resetMacroSkip();

        if (lotusEngine_) {
            isPrevSpace_       = false;
            isPrevHyphen_      = false;
            shouldCapitalize_  = false;
            isPrevPunctuation_ = false;
            if (realMode == LotusMode::Preedit && isFocusOut) {
                (*lotusEngine_).commitPreedit();
                const std::u32string commitText = pullCommitString();
                if (!commitText.empty()) {
                    lotus::fcitx_text::commit(&ic_, commitText, last_commit_time_);
                }
            }
            (*lotusEngine_).reset();
            oldPreBuffer_.clear();
            hasHistory_ = false;
        }
        if (getFrontendName(&ic_) != "dbus")
            clearAllBuffers();

        switch (realMode) {
            case LotusMode::Preedit: {
                ic_.inputPanel().reset();
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_.updatePreedit();
                break;
            }
            case LotusMode::SurroundingText:
            case LotusMode::Smooth:
            case LotusMode::Minecraft:
            case LotusMode::SuperSmooth:
            case LotusMode::Select: {
                ic_.inputPanel().reset();
                break;
            }
            case LotusMode::Emoji: {
                ic_.inputPanel().reset();
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_.updatePreedit();
                break;
            }
            default: {
                break;
            }
        }
    }

    void LotusState::commitBuffer() {
        switch (realMode) {
            case LotusMode::Preedit: {
                ic_.inputPanel().reset();
                if (lotusEngine_) {
                    (*lotusEngine_).commitPreedit();
                    const std::u32string commitText = pullCommitString();
                    lotus::fcitx_text::commit(&ic_, commitText, last_commit_time_);
                    (*lotusEngine_).reset();
                }
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_.updatePreedit();
                break;
            }
            case LotusMode::Smooth:
            case LotusMode::SurroundingText:
            case LotusMode::Minecraft:
            case LotusMode::SuperSmooth:
            case LotusMode::Select: {
                if (lotusEngine_) {
                    (*lotusEngine_).reset();
                }
                break;
            }
            default: {
                break;
            }
        }
    }

    void LotusState::clearAllBuffers() {
        LOTUS_DEBUG("Clear all buffers");
        if (is_deleting_.load(std::memory_order_acquire)) {
            return;
        }
        resetMacroSkip();
        oldPreBuffer_.clear();
        hasHistory_ = false;
        if (!is_deleting_.load(std::memory_order_acquire)) {
            expected_backspaces_     = 0;
            current_backspace_count_ = 0;
            pending_commit_string_.clear();
        }
        emojiBuffer_.clear();
        emojiCandidates_.clear();
        buffered_keys_.clear();
        shouldCapitalize_  = false;
        isPrevSpace_       = false;
        isPrevHyphen_      = false;
        isPrevPunctuation_ = false;
        if (lotusEngine_)
            (*lotusEngine_).reset();
    }

    bool LotusState::isEmptyHistory() const {
        return !hasHistory_;
    }

    void LotusState::replayBufferedKeys() {
        LOTUS_INFO("Starting replay buffered keys");
        if (buffered_keys_.empty()) {
            return;
        }
        auto keys = std::move(buffered_keys_);
        buffered_keys_.clear();
        for (size_t i = 0; i < keys.size(); ++i) {
            auto           sym          = static_cast<KeySym>(keys[i].sym);
            uint32_t       state        = keys[i].state;
            const uint32_t keyCodePoint = Key::keySymToUnicode(sym);
            if (keyCodePoint == 0U) {
                continue;
            }
            const std::u32string keyText(1, static_cast<char32_t>(keyCodePoint));

            bool                 processed = lotusEngine_->processKey(engineKeyValue(sym), lotus::keyStatesFromBits(state));

            const std::u32string commitStr = pullCommitString();
            if (!commitStr.empty()) {
                DiffPart diffPart = compareAndSplitU32(oldPreBuffer_, commitStr);

                if (!diffPart.deletedPart.empty()) {
                    // Re-buffer remaining keys for next replay cycle.
                    for (size_t j = i + 1; j < keys.size(); ++j) {
                        if (buffered_keys_.size() < MAX_BUFFERED_KEYS) {
                            buffered_keys_.push_back(keys[j]);
                        }
                    }
                    performReplacement(diffPart);
                    hasHistory_ = false;
                    (*lotusEngine_).reset();
                    oldPreBuffer_.clear();
                    return;
                }
                if (!diffPart.addedPart.empty()) {
                    lotus::fcitx_text::commit(&ic_, diffPart.addedPart, last_commit_time_);
                }

                hasHistory_ = false;
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
                continue;
            }

            if (!processed) {
                lotus::fcitx_text::commit(&ic_, keyText, last_commit_time_);
                continue;
            }

            hasHistory_ = true;
            realtextLen.fetch_add(1, std::memory_order_acq_rel);

            const std::u32string preeditStr = pullPreeditString();

            DiffPart             diffPart = compareAndSplitU32(oldPreBuffer_, preeditStr);
            if (diffPart.deletedPart.empty()) {
                if (!diffPart.addedPart.empty()) {
                    lotus::fcitx_text::commit(&ic_, diffPart.addedPart, last_commit_time_);
                    oldPreBuffer_ = preeditStr;
                }
            } else {
                if (!ensureUinputConnected()) {
                    lotus::fcitx_text::commit(&ic_, keyText, last_commit_time_);
                    continue;
                }

                if (is_deleting_.load()) {
                    is_deleting_.store(false, std::memory_order_release);
                }

                // Re-buffer remaining keys for next replay cycle.
                for (size_t j = i + 1; j < keys.size(); ++j) {
                    if (buffered_keys_.size() < MAX_BUFFERED_KEYS) {
                        buffered_keys_.push_back(keys[j]);
                    }
                }
                performReplacement(diffPart);
                oldPreBuffer_ = preeditStr;
                return;
            }
        }
        LOTUS_INFO("Replay buffered keys done");
    }

    bool LotusState::isMacroSkipModifier(KeySym sym) const {
        const auto trigger = engine_.config().macroSkipTriggerModifier.value();
        switch (trigger) {
            case MacroSkipTriggerModifier::Shift: return sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R;
            case MacroSkipTriggerModifier::Ctrl: return sym == FcitxKey_Control_L || sym == FcitxKey_Control_R;
            case MacroSkipTriggerModifier::Alt: return sym == FcitxKey_Alt_L || sym == FcitxKey_Alt_R;
            case MacroSkipTriggerModifier::Disabled:
            default: return false;
        }
    }

    void LotusState::handleModifierTap(const KeyEvent& keyEvent) {
        const auto trigger = engine_.config().macroSkipTriggerModifier.value();
        if (trigger == MacroSkipTriggerModifier::Disabled || !*engine_.config().enableMacro) {
            return;
        }
        if (!isMacroSkipModifier(keyEvent.rawKey().sym())) {
            tracking_modifier_tap_ = false;
            return;
        }
        if (keyEvent.isRelease()) {
            if (tracking_modifier_tap_) {
                tracking_modifier_tap_ = false;
                macro_skip_            = true;
                lotusEngine_->setMacroEnabled(false);
                LOTUS_INFO("Macro skip enabled for next word");
            }
        } else {
            tracking_modifier_tap_ = true;
        }
    }

    void LotusState::cancelModifierTap() {
        tracking_modifier_tap_ = false;
    }

    void LotusState::reEnableMacroAfterWordEnd() {
        if (!macro_skip_) {
            return;
        }
        if (!pullPreeditString().empty()) {
            return;
        }
        macro_skip_ = false;
        lotusEngine_->setMacroEnabled(*engine_.config().enableMacro);
    }

    void LotusState::resetMacroSkip() {
        tracking_modifier_tap_ = false;
        macro_skip_            = false;
        if (lotusEngine_) {
            lotusEngine_->setMacroEnabled(*engine_.config().enableMacro);
        }
    }

    KeySym LotusState::unshiftKeySym(KeySym sym) {
        if (sym >= FcitxKey_A && sym <= FcitxKey_Z) {
            return static_cast<KeySym>(sym + (FcitxKey_a - FcitxKey_A));
        }
        switch (sym) {
            case FcitxKey_asciitilde: return FcitxKey_grave;
            case FcitxKey_exclam: return FcitxKey_1;
            case FcitxKey_at: return FcitxKey_2;
            case FcitxKey_numbersign: return FcitxKey_3;
            case FcitxKey_dollar: return FcitxKey_4;
            case FcitxKey_percent: return FcitxKey_5;
            case FcitxKey_asciicircum: return FcitxKey_6;
            case FcitxKey_ampersand: return FcitxKey_7;
            case FcitxKey_asterisk: return FcitxKey_8;
            case FcitxKey_parenleft: return FcitxKey_9;
            case FcitxKey_parenright: return FcitxKey_0;
            case FcitxKey_underscore: return FcitxKey_minus;
            case FcitxKey_plus: return FcitxKey_equal;
            case FcitxKey_braceleft: return FcitxKey_bracketleft;
            case FcitxKey_braceright: return FcitxKey_bracketright;
            case FcitxKey_bar: return FcitxKey_backslash;
            case FcitxKey_colon: return FcitxKey_semicolon;
            case FcitxKey_quotedbl: return FcitxKey_apostrophe;
            case FcitxKey_less: return FcitxKey_comma;
            case FcitxKey_greater: return FcitxKey_period;
            case FcitxKey_question: return FcitxKey_slash;
            default: return sym;
        }
    }

} // namespace fcitx
