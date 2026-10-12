/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-state.h"
#include "lotus-engine.h"
#include "lotus-candidates.h"
#include "lotus-key-pacer.h"
#include "lotus-utils.h"

#include "bamboo-core.h" // generated cgo header; only included where the bridge is called

#include <linux/input-event-codes.h>

#include <cstddef>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterface.h>

#include <algorithm>
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

    void commitStringWithLog(fcitx::InputContext* ic, const std::string& text) {
        ic->commitString(text);
        LOTUS_INFO("Commit: " + text);
    }

    bool isWordBreak(uint32_t ucs4) {
        // Space, tab, newline, carriage return, null, or punctuation/symbols (: ; < = > ? @)
        return ucs4 == ' ' || ucs4 == '\t' || ucs4 == '\n' || ucs4 == '\r' || ucs4 == 0 || (ucs4 >= 58 && ucs4 <= 64);
    }

    LotusState::LotusState(LotusEngine* engine, InputContext* ic) : engine_(engine), ic_(ic) {
        setEngine();
    }

    LotusState::~LotusState() {
        KeyPacer::instance().clearBarrierOwner(this);
    }

    void LotusState::setEngine() {
        lotusEngine_.reset();

        if (engine_->config().inputMethod.value() == "Custom") {
            const auto&        keymaps = *engine_->customKeymap().customKeymap;
            std::vector<char*> charArray;
            charArray.reserve((keymaps.size() * 2) + 1);
            for (const auto& keymap : keymaps) {
                charArray.push_back(const_cast<char*>(keymap.key->data()));   //NOLINT
                charArray.push_back(const_cast<char*>(keymap.value->data())); //NOLINT
            }
            charArray.push_back(nullptr);
            lotusEngine_.reset(NewCustomEngine(charArray.data(), engine_->dictionary(), engine_->macroTable()));
        } else {
            lotusEngine_.reset(NewEngine(engine_->config().inputMethod->data(), engine_->dictionary(), engine_->macroTable()));
        }
        setOption();
        resetMacroSkip();
    }

    void LotusState::setOption() {
        if (!lotusEngine_)
            return;
        FcitxBambooEngineOption option = {
            .autoNonVnRestore    = *engine_->config().autoNonVnRestore,
            .ddFreeStyle         = *engine_->config().ddFreeStyle,
            .macroEnabled        = *engine_->config().enableMacro,
            .autoCapitalizeMacro = *engine_->config().capitalizeMacro,
            .spellCheckWithDicts = *engine_->config().spellCheck,
            .outputCharset       = engine_->config().outputCharset->data(),
            .modernStyle         = *engine_->config().modernStyle,
            .freeMarking         = *engine_->config().freeMarking,
            .w2u                 = static_cast<int>(*engine_->config().w2u),
            .bracketTransform    = static_cast<int>(*engine_->config().bracketTransform),
            .timeFormat          = engine_->config().timeFormat->data(),
            .dateFormat          = engine_->config().dateFormat->data(),
        };

        EngineSetOption(lotusEngine_.handle(), &option);
    }

    bool LotusState::ensureUinputConnected() {
        return KeyPacer::instance().ensureConnected();
    }

    void LotusState::enqueueBackspace(size_t count, uint32_t pre_delay, uint32_t interval, uint32_t post_delay) {
        if (count == 0)
            return;
        KeyPacer::instance().push(buildBackspaceJob(count, std::chrono::milliseconds{pre_delay}, std::chrono::milliseconds{interval}, std::chrono::milliseconds{post_delay}));
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

        auto               textRange         = fcitx::utf8::MakeUTF8CharRange(s.text());
        auto               oldPreBufferRange = fcitx::utf8::MakeUTF8CharRange(oldPreBuffer_);
        std::u32string     u32Text(textRange.begin(), textRange.end());
        std::u32string     u32OldPreBuffer(oldPreBufferRange.begin(), oldPreBufferRange.end());

        const unsigned int cursor  = s.cursor();
        const unsigned int anchor  = s.anchor();
        const size_t       textLen = u32Text.length();

        // Fix that surrounding text is delay update
        const size_t buffLen    = u32OldPreBuffer.length();
        const size_t pb         = u32Text.find(u32OldPreBuffer);
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
        if (EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U)
            keyEvent.filterAndAccept();
        const std::string commitText = pullCommitString();
        if (!commitText.empty()) {
            LOTUS_INFO("Commit: " + commitText);
            ic_->commitString(commitText);
        }
        ic_->inputPanel().reset();
        const std::string preeditText = pullPreeditString();
        if (!preeditText.empty()) {
            std::string_view view = preeditText;
            Text             text;
            TextFormatFlags  fmt = TextFormatFlag::NoFlag;
            if (utf8::validate(view))
                text.append(std::string(view), fmt);
            text.setCursor(static_cast<int>(text.textLength()));
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit))
                ic_->inputPanel().setClientPreedit(text);
            else
                ic_->inputPanel().setPreedit(text);
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
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
        ic_->inputPanel().setAuxDown(Text(status));
    }

    void LotusState::handleEmojiMode(KeyEvent& keyEvent) {
        const KeySym currentSym      = keyEvent.rawKey().sym();
        bool         isCtrlBackspace = isBackspace(currentSym) && ((keyEvent.rawKey().states() & KeyState::Ctrl) != 0U);

        if (keyEvent.key().hasModifier() && !isCtrlBackspace) {
            keyEvent.forward();
            return;
        }

        auto baseList   = ic_->inputPanel().candidateList();
        auto commonList = std::dynamic_pointer_cast<CommonCandidateList>(baseList);
        if (commonList && currentSym >= FcitxKey_1 && currentSym <= FcitxKey_9) {
            int offset      = currentSym - FcitxKey_1;
            int globalIndex = (commonList->currentPage() * commonList->pageSize()) + offset;

            if (globalIndex < commonList->totalSize()) {
                commonList->candidateFromAll(globalIndex).select(ic_);
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
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }
        }

        if (isBackspace(currentSym)) {
            if (!emojiBuffer_.empty()) {
                if (isCtrlBackspace) {
                    emojiBuffer_.clear();
                } else {
                    eraseLastUtf8Codepoint(emojiBuffer_);
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
                    commonList->candidateFromAll(globalIdx).select(ic_);
                    keyEvent.filterAndAccept();
                } else if (currentSym == FcitxKey_Return && !emojiBuffer_.empty()) {
                    ic_->commitString(emojiBuffer_);
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
                ic_->inputPanel().reset();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }

            default: break;
        }

        {
            std::string utf8Char = Key::keySymToUTF8(currentSym);
            if (!utf8Char.empty()) {
                emojiBuffer_.append(utf8Char);
                keyEvent.filterAndAccept();
                updateEmojiPreedit();
            } else {
                keyEvent.forward();
            }
        }
    }
    void LotusState::updateEmojiPreedit() {
        if (emojiBuffer_.empty()) {
            emojiCandidates_ = engine_->emojiLoader().history();
            if (emojiCandidates_.empty()) {
                ic_->inputPanel().reset();
                ic_->updatePreedit();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
        } else {
            emojiCandidates_ = engine_->emojiLoader().search(emojiBuffer_);
        }

        if (!emojiBuffer_.empty()) {
            Text preeditText;
            preeditText.append(emojiBuffer_, TextFormatFlag::Underline);
            preeditText.setCursor(static_cast<int>(preeditText.textLength()));
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit))
                ic_->inputPanel().setClientPreedit(preeditText);
            else
                ic_->inputPanel().setPreedit(preeditText);
        } else {
            ic_->inputPanel().setClientPreedit(Text());
            ic_->inputPanel().setPreedit(Text());
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

            ic_->inputPanel().setCandidateList(std::move(candidateList));
            auto currentList = std::dynamic_pointer_cast<CommonCandidateList>(ic_->inputPanel().candidateList());
            updateEmojiPageStatus(currentList.get());
        } else {
            ic_->inputPanel().setCandidateList(nullptr);
        }

        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void LotusState::commitPendingReplacement() {
        // Validate surr cursor pos should match realtextLen after all BS/Left applied
        const auto& surr = ic_->surroundingText();
        if (surr.isValid() && surr.cursor() == realtextLen.load(std::memory_order_acquire)) {
            LOTUS_INFO("Skip retry");
        } else {
            // Retry x3 (2 ms each), khi can (chromium,electron,...)
            for (int retry = 0; retry < 3; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                const auto& surr2 = ic_->surroundingText();
                if (surr2.isValid() && surr2.cursor() == realtextLen.load(std::memory_order_acquire)) {
                    break;
                }
            }
        }
        std::string commitText = std::move(pending_commit_string_);
        pending_commit_string_.clear();

        if (!commitText.empty()) {
            ic_->commitString(commitText);
            updateLastCommitTime();
            LOTUS_INFO("Commit: " + commitText);
        }
    }

    void LotusState::finishReplacement(KeyEvent& event) {
        commitPendingReplacement();
        expected_backspaces_     = 0;
        current_backspace_count_ = 0;
        event.filterAndAccept();
        is_deleting_.store(false, std::memory_order_release);
        replayBufferedKeys();
    }

    uint32_t LotusState::echoSequence() const {
        return static_cast<uint32_t>(current_backspace_count_);
    }

    void LotusState::commitFromChain(const std::string& text) {
        // The chain text is authoritative (same value performReplacement stored,
        // re-stated here so aborts/concurrency can't substitute a stale string).
        LOTUS_INFO("Chain barrier lifted, committing: " + text);
        pending_commit_string_ = text;
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
        KeyPacer::instance().notifyEcho(current_backspace_count_);
        if (current_backspace_count_ < expected_backspaces_) {
            return false; // Allow intermediate backspaces/lefts to reach the app to clear autofill/old text / extend the selection.
        }
        if (isSelectEcho) {
            LOTUS_INFO("Last select echo swallowed, commit held for barrier: " + pending_commit_string_);
            event.filterAndAccept();
            return true;
        }
        finishReplacement(event);
        return true;
    }

    void LotusState::performReplacement(const std::string& deletedPart, const std::string& addedPart) {
        const int  commitMs = clampDelay(appRuleSetting_.commitInterval > 0 ? appRuleSetting_.commitInterval : *engine_->config().defaultCommitInterval);
        const auto interval =
            static_cast<uint32_t>(clampDelay(appRuleSetting_.backspaceInterval > 0 ? appRuleSetting_.backspaceInterval : *engine_->config().defaultBackspaceInterval));
        uint32_t pre_delay = 0;
        if (last_commit_time_.time_since_epoch().count() > 0) {
            auto now       = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_commit_time_).count();
            if (elapsedMs < commitMs) {
                pre_delay = static_cast<uint32_t>(commitMs - elapsedMs);
            }
        }
        LOTUS_INFO("Perform replacement: " + deletedPart + " -> " + addedPart); //NOLINT
        // A new job replaces any in-flight one: drop the old queue first, or
        // its stale barrier/commit could fire after the overwrite. No-op when
        // the queue already drained (the normal case).
        KeyPacer::instance().clear();
        KeyPacer::instance().setBarrierOwner(this);
        current_backspace_count_       = 0;
        pending_commit_string_         = addedPart;
        expected_backspaces_           = utf8::length(deletedPart);
        const auto& surrounding        = ic_->surroundingText();
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
        const auto post_delay = static_cast<uint32_t>(clampDelay(appRuleSetting_.postDelay > 0 ? appRuleSetting_.postDelay : *engine_->config().defaultPostDelay));
        // Select mode: the uinput server selects with Shift+Left instead. The
        // trigger-key compensation above still applies because the last echoed
        // Left is swallowed; autofill compensation does not, see above.
        if (realMode == LotusMode::Select && !isAutofillCertain_) {
            enqueueSelect(expected_backspaces_, pre_delay, interval, post_delay, addedPart);
            LOTUS_INFO("Send select of " + std::to_string(expected_backspaces_) + " characters");
            return;
        }
        enqueueBackspace(expected_backspaces_, pre_delay, interval, post_delay);
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
            ResetEngine(lotusEngine_.handle());
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

    void LotusState::commitOrReplace(const std::string& deleted, const std::string& added, const std::string& logMsg) {
        if (realMode == LotusMode::SurroundingText) {
            ic_->deleteSurroundingText(-1, 1);
            ic_->commitString(added);
        } else { // Uinput, Smooth, Preedit, etc.
            performReplacement(deleted, added);
        }
        LOTUS_INFO(logMsg);
    }

    std::string LotusState::pullCommitString() {
        UniqueCPtr<char> commit(EnginePullCommit(lotusEngine_.handle()));
        return (commit && (*commit.get() != 0)) ? std::string(commit.get()) : std::string{};
    }

    std::string LotusState::pullPreeditString() {
        UniqueCPtr<char> preedit(EnginePullPreedit(lotusEngine_.handle()));
        return (preedit && (*preedit.get() != 0)) ? std::string(preedit.get()) : std::string{};
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
                EngineProcessKeyEvent(lotusEngine_.handle(), FcitxKey_BackSpace, 0);
                oldPreBuffer_ = pullPreeditString();
            } else {
                hasHistory_ = false;
                ResetEngine(lotusEngine_.handle());
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
            return;
        }

        std::string keyUtf8 = Key::keySymToUTF8(currentSym);
        if (keyUtf8.empty()) {
            keyEvent.forward();
            return;
        }

        bool              processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;

        const std::string commitStr = pullCommitString();
        if (!commitStr.empty()) {
            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldPreBuffer_, commitStr, deletedPart, addedPart);

            if (!deletedPart.empty()) {
                performReplacement(deletedPart, addedPart);
                keyEvent.filterAndAccept();
            } else {
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!addedPart.empty() && (keyUtf8 != addedPart || wasAutoCapitalized)) {
                    // Prevent auto-capitalized character replacement from stripping out Vietnamese chars
                    if (addedPart.size() > 1 && addedPart.back() == ' ') {
                        // Stripping the trigger key (space) from addedPart
#if __cplusplus >= 202002L
                        addedPart.resize(addedPart.size() - 1);
#else
                        addedPart = addedPart.substr(0, addedPart.size() - 1);
#endif
                    }
                    commitStringWithLog(ic_, addedPart);
                    keyEvent.filterAndAccept();
                } else {
                    keyEvent.forward();
                }
            }

            hasHistory_ = false;
            ResetEngine(lotusEngine_.handle());
            oldPreBuffer_.clear();

            return;
        }

        if (!processed) {
            const std::string preeditCurrent = pullPreeditString();
            if (preeditCurrent.empty()) {
                hasHistory_ = false;
                ResetEngine(lotusEngine_.handle());
                oldPreBuffer_.clear();
                keyEvent.forward();
            }
            return;
        }

        hasHistory_ = true;
        realtextLen.fetch_add(1, std::memory_order_acq_rel);

        const std::string preeditStr = pullPreeditString();

        std::string       deletedPart;
        std::string       addedPart;

        if (compareAndSplitStrings(oldPreBuffer_, preeditStr, deletedPart, addedPart) != 0) {
            if (deletedPart.empty()) {
                bool isCommit           = false;
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!addedPart.empty()) {
                    oldPreBuffer_ = preeditStr;
                    if (wasAutoCapitalized || addedPart != keyUtf8) {
                        commitStringWithLog(ic_, addedPart);
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
                        ic_->commitString(rawKey);
                    }
                    return;
                }

                if (is_deleting_.load()) {
                    is_deleting_.store(false, std::memory_order_release);
                    KeyPacer::instance().clear();
                }

                keyEvent.filterAndAccept();
                performReplacement(deletedPart, addedPart);
                oldPreBuffer_ = preeditStr;
            }
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
            ResetEngine(lotusEngine_.handle());
            keyEvent.forward();
            return;
        }

        const std::string& text   = surrounding.text();
        unsigned int       cursor = std::min(surrounding.anchor(), surrounding.cursor());

        size_t             textLen = utf8::lengthValidated(text);

        if (textLen == utf8::INVALID_LENGTH || cursor <= 0 || cursor > textLen) {
            processNormalKey(keyEvent, currentSym);
            return;
        }

        {
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

                uint32_t ucs4 = utf8::getChar(prev, text.end());

                if (isWordBreak(ucs4))
                    break;

                startIter = prev;
                ++scanCount;
            }

            std::string oldWord(startIter, endIter);

            if (oldWord.empty()) {
                processNormalKey(keyEvent, currentSym);
                return;
            }

            EngineRebuildFromText(lotusEngine_.handle(), oldWord.c_str());

            bool processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;

            if (!processed) {
                keyEvent.forward();
                ResetEngine(lotusEngine_.handle());
                return;
            }

            std::string newWord = pullCommitString();
            newWord += pullPreeditString();

            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldWord, newWord, deletedPart, addedPart);
            if ((deletedPart.empty() || deletedPart == oldWord) && addedPart == keyEvent.key().toString()) {
                ResetEngine(lotusEngine_.handle());
                keyEvent.forward();
                return;
            }

            if (!deletedPart.empty() || !addedPart.empty()) {
                size_t charsToDelete = utf8::length(deletedPart);

                if (charsToDelete > 0) {
                    ic->deleteSurroundingText(-static_cast<int>(charsToDelete), static_cast<int>(charsToDelete));
                }

                if (!addedPart.empty()) {
                    commitStringWithLog(ic, addedPart);
                }

                ResetEngine(lotusEngine_.handle());
                keyEvent.filterAndAccept();
                return;
            }

            ResetEngine(lotusEngine_.handle());
            keyEvent.filterAndAccept();
            return;
        }
    }

    void LotusState::processNormalKey(KeyEvent& keyEvent, KeySym currentSym) {
        auto* ic = keyEvent.inputContext();
        ResetEngine(lotusEngine_.handle());
        bool processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;
        if (processed) {
            std::string out = pullCommitString();
            out += pullPreeditString();

            if (!out.empty()) {
                LOTUS_INFO("Commit: " + out);
                ic->commitString(out);
            }

            ResetEngine(lotusEngine_.handle());
            keyEvent.filterAndAccept();
        } else {
            keyEvent.forward();
        }
    }

    void LotusState::handleDoubleSpaceReplacement() {
        commitOrReplace(" ", ". ", "Commit: . ");
        if (*engine_->config().autoCapitalizeAfterPunctuation) {
            isPrevPunctuation_ = true;
            shouldCapitalize_  = true;
        }
    }

    void LotusState::handleDoubleHyphenReplacement() {
        // Em-dash (U+2014)
        std::string emDash = "—";
        commitOrReplace("-", emDash, "Commit: — (em-dash)");
    }

    void LotusState::handleOffModeMacro(KeyEvent& keyEvent, KeySym currentSym) {
        if (forwardSpecialKey(keyEvent, currentSym))
            return;

        if (!ensureUinputConnected()) {
            // Best effort only; macro path below falls back when offline.
        }

        if (isBackspace(currentSym)) {
            EngineProcessKeyEvent(lotusEngine_.handle(), FcitxKey_BackSpace, 0);
            oldPreBuffer_ = pullPreeditString();
            keyEvent.forward();
            return;
        }

        if (currentSym == FcitxKey_Return) {
            if (!oldPreBuffer_.empty()) {
                ResetEngine(lotusEngine_.handle());
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
            return;
        }

        std::string       keyUtf8 = Key::keySymToUTF8(currentSym);

        bool              processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;

        const std::string commitStr = pullCommitString();
        if (processed && !commitStr.empty()) {

            // Determine if this is a macro expansion or just confirmed typed text
            bool isMacroExpansion = false;
            if (keyUtf8.empty()) {
                isMacroExpansion = (commitStr != oldPreBuffer_);
            } else {
                isMacroExpansion = (commitStr != oldPreBuffer_ + keyUtf8);
            }

            if (isMacroExpansion) {
                LOTUS_INFO("Macro expansion: '" + oldPreBuffer_ + "' -> '" + commitStr + "'");
                // Try uinput replacement first, fallback to deleteSurroundingText, then plain commit
                if (ensureUinputConnected() && !oldPreBuffer_.empty()) {
                    performReplacement(oldPreBuffer_, commitStr);
                } else if (ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                    const auto& surrounding = ic_->surroundingText();
                    if (surrounding.isValid()) {
                        size_t oldLen = utf8::length(oldPreBuffer_);
                        if (oldLen > 0) {
                            ic_->deleteSurroundingText(-static_cast<int>(oldLen), static_cast<int>(oldLen));
                        }
                        ic_->commitString(commitStr);
                    } else {
                        ic_->commitString(commitStr);
                    }
                } else {
                    ic_->commitString(commitStr);
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
                ResetEngine(lotusEngine_.handle());
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
        if (!KeyPacer::instance().isWaiting() && !KeyPacer::instance().hasPendingBarrierFor(this) && current_backspace_count_ >= expected_backspaces_ &&
            is_deleting_.load()) {
            LOTUS_WARN("Aborting in-flight replacement: count=" + std::to_string(current_backspace_count_) + " expected=" +
                       std::to_string(expected_backspaces_) + " pending=" + pending_commit_string_);
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
        if (*engine_->config().autoCapitalizeAfterPunctuation && realMode != LotusMode::Off) {
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
                        LOTUS_INFO("Delete echo counted: " + std::to_string(current_backspace_count_) + "/" + std::to_string(expected_backspaces_));
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
        if (*engine_->config().doubleSpaceToPeriod && realMode != LotusMode::Off) {
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
        if (*engine_->config().doubleHyphenToEmDash && realMode != LotusMode::Off) {
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
                if (*engine_->config().enableMacroInOffMode && *engine_->config().enableMacro) {
                    handleOffModeMacro(keyEvent, currentSym);
                }
                break;
            }
        }
    }

    void LotusState::reset(bool isFocusOut) {
        const auto& surrounding = ic_->surroundingText();
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
                EngineCommitPreedit(lotusEngine_.handle());
                const std::string commitText = pullCommitString();
                if (!commitText.empty()) {
                    commitStringWithLog(ic_, commitText);
                }
            }
            ResetEngine(lotusEngine_.handle());
            oldPreBuffer_.clear();
            hasHistory_ = false;
        }
        if (getFrontendName(ic_) != "dbus")
            clearAllBuffers();

        switch (realMode) {
            case LotusMode::Preedit: {
                ic_->inputPanel().reset();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            }
            case LotusMode::SurroundingText:
            case LotusMode::Smooth:
            case LotusMode::Minecraft:
            case LotusMode::SuperSmooth:
            case LotusMode::Select: {
                ic_->inputPanel().reset();
                break;
            }
            case LotusMode::Emoji: {
                ic_->inputPanel().reset();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
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
                ic_->inputPanel().reset();
                if (lotusEngine_) {
                    EngineCommitPreedit(lotusEngine_.handle());
                    const std::string commitText = pullCommitString();
                    if (!commitText.empty())
                        ic_->commitString(commitText);
                    ResetEngine(lotusEngine_.handle());
                }
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            }
            case LotusMode::Smooth:
            case LotusMode::SurroundingText:
            case LotusMode::Minecraft:
            case LotusMode::SuperSmooth:
            case LotusMode::Select: {
                if (lotusEngine_) {
                    ResetEngine(lotusEngine_.handle());
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
            ResetEngine(lotusEngine_.handle());
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
            auto        sym     = static_cast<KeySym>(keys[i].sym);
            uint32_t    state   = keys[i].state;
            std::string keyUtf8 = Key::keySymToUTF8(sym);
            if (keyUtf8.empty()) {
                continue;
            }

            bool              processed = EngineProcessKeyEvent(lotusEngine_.handle(), sym, state) != 0U;

            const std::string commitStr = pullCommitString();
            if (!commitStr.empty()) {
                std::string deletedPart;
                std::string addedPart;
                compareAndSplitStrings(oldPreBuffer_, commitStr, deletedPart, addedPart);

                if (!deletedPart.empty()) {
                    // Re-buffer remaining keys for next replay cycle.
                    for (size_t j = i + 1; j < keys.size(); ++j) {
                        if (buffered_keys_.size() < MAX_BUFFERED_KEYS) {
                            buffered_keys_.push_back(keys[j]);
                        }
                    }
                    performReplacement(deletedPart, addedPart);
                    hasHistory_ = false;
                    ResetEngine(lotusEngine_.handle());
                    oldPreBuffer_.clear();
                    return;
                }
                if (!addedPart.empty()) {
                    ic_->commitString(addedPart);
                }

                hasHistory_ = false;
                ResetEngine(lotusEngine_.handle());
                oldPreBuffer_.clear();
                continue;
            }

            if (!processed) {
                ic_->commitString(keyUtf8);
                continue;
            }

            hasHistory_ = true;
            realtextLen.fetch_add(1, std::memory_order_acq_rel);

            const std::string preeditStr = pullPreeditString();

            std::string       deletedPart;
            std::string       addedPart;
            if (compareAndSplitStrings(oldPreBuffer_, preeditStr, deletedPart, addedPart) != 0) {
                if (deletedPart.empty()) {
                    if (!addedPart.empty()) {
                        ic_->commitString(addedPart);
                        oldPreBuffer_ = preeditStr;
                    }
                } else {
                    if (!ensureUinputConnected()) {
                        ic_->commitString(keyUtf8);
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
                    performReplacement(deletedPart, addedPart);
                    oldPreBuffer_ = preeditStr;
                    return;
                }
            }
        }
        LOTUS_INFO("Replay buffered keys done");
    }

    bool LotusState::isMacroSkipModifier(KeySym sym) const {
        const auto trigger = engine_->config().macroSkipTriggerModifier.value();
        switch (trigger) {
            case MacroSkipTriggerModifier::Shift: return sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R;
            case MacroSkipTriggerModifier::Ctrl: return sym == FcitxKey_Control_L || sym == FcitxKey_Control_R;
            case MacroSkipTriggerModifier::Alt: return sym == FcitxKey_Alt_L || sym == FcitxKey_Alt_R;
            case MacroSkipTriggerModifier::Disabled:
            default: return false;
        }
    }

    void LotusState::handleModifierTap(const KeyEvent& keyEvent) {
        const auto trigger = engine_->config().macroSkipTriggerModifier.value();
        if (trigger == MacroSkipTriggerModifier::Disabled || !*engine_->config().enableMacro) {
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
                EngineSetMacroEnabled(lotusEngine_.handle(), 0);
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
        EngineSetMacroEnabled(lotusEngine_.handle(), *engine_->config().enableMacro ? 1 : 0);
    }

    void LotusState::resetMacroSkip() {
        tracking_modifier_tap_ = false;
        macro_skip_            = false;
        if (lotusEngine_) {
            EngineSetMacroEnabled(lotusEngine_.handle(), *engine_->config().enableMacro ? 1 : 0);
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

    void LotusState::updateLastCommitTime() {
        last_commit_time_ = std::chrono::steady_clock::now();
    }
} // namespace fcitx
