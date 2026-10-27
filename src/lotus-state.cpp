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
#include "lotus-log.h"
#include "lotus-utils.h"

#include "emoji.h"
#include "input_method.h"
#include "utf8.h"

#include <cstddef>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterface.h>

#include <algorithm>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

namespace fcitx {
    constexpr int     kMaxScanLength         = 15;
    constexpr int64_t kReplacementAckSlackMs = 200;

    constexpr int     kMinDelayMs = 1;
    constexpr int     kMaxDelayMs = 1000;

    int               clampDelay(int value) {
        return std::clamp(value, kMinDelayMs, kMaxDelayMs);
    }

    bool isWordBreak(uint32_t ucs4) {
        // Space, tab, newline, carriage return, null, or punctuation/symbols (: ; < = > ? @)
        return ucs4 == ' ' || ucs4 == '\t' || ucs4 == '\n' || ucs4 == '\r' || ucs4 == 0 || (ucs4 >= 58 && ucs4 <= 64);
    }

    LotusState::LotusState(LotusEngine* engine, InputContext* ic) : engine_(*engine), ic_(*ic) {
        setEngine();
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

    int LotusState::setupUinput() {
        return ensureUinputServerConnected() ? g_uinputClientFd.load(std::memory_order_acquire) : -1;
    }

    void LotusState::sendKbMsg(KbOp op, size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay) {
        if (g_uinputClientFd.load(std::memory_order_acquire) < 0 && !ensureUinputServerConnected()) {
            LOTUS_ERROR("Cannot send key request since cannot connect to uinput server");
            return;
        }

        const KbMsg msg{.op = op, .count = count, .preDelay = preDelay, .interval = interval, .postDelay = postDelay};
        ssize_t     n = send(g_uinputClientFd.load(std::memory_order_acquire), &msg, sizeof(msg), MSG_NOSIGNAL);

        if (n < 0) {
            LOTUS_WARN("Failed to send key request: " + std::string(strerror(errno)));
            FdGuard oldFd(g_uinputClientFd.exchange(-1, std::memory_order_acq_rel));
            if (ensureUinputServerConnected()) {
                LOTUS_INFO("Reconnected to uinput server successfully");
                send(g_uinputClientFd.load(std::memory_order_acquire), &msg, sizeof(msg), MSG_NOSIGNAL);
            }
        }
    }

    void LotusState::sendBackspaceUinput(size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay) {
        sendKbMsg(KbOp::Backspace, count, preDelay, interval, postDelay);
    }

    void LotusState::sendSelectUinput(size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay) {
        sendKbMsg(KbOp::Select, count, preDelay, interval, postDelay);
    }

    void LotusState::cancelPendingUinput() {
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        sendKbMsg(KbOp::Cancel, 0, 0, 0, 0);
        LOTUS_INFO("Cancel the burst still pending on the uinput server");
    }

    void LotusState::abortPendingUinputReplacement() {
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        cancelPendingUinput();
        // The server finishes the selection on Cancel, so the old text is always
        // fully removed: stash the replacement text for the refocus commit.
        stashedCommit_      = pendingCommitString_;
        stashedNewWord_     = oldPreBuffer_;
        uinputAbortTimeMs_  = nowMs();
        uinputAbortPending_ = !stashedCommit_.empty() || !bufferedKeys_.empty();
        pendingCommitString_.clear();
        expectedBackspaces_    = 0;
        currentBackspaceCount_ = 0;
        replacementDeadlineMs_ = 0;
        realSelect_            = false;
        deletionInterruptedAt_ = 0;
        g_isDeleting.store(false, std::memory_order_release);

        oldPreBuffer_.clear();
        hasHistory_   = false;
        deferCommits_ = getFrontendName(&ic_) == "dbus";
    }

    void LotusState::dropAbortedUinputStash() {
        stashedCommit_.clear();
        stashedNewWord_.clear();
        uinputAbortTimeMs_ = 0;
        (*lotusEngine_).reset();
        oldPreBuffer_.clear();
        hasHistory_   = false;
        deferCommits_ = getFrontendName(&ic_) == "dbus";
        if (!bufferedKeys_.empty()) {
            replayBufferedKeys();
        }
        flushCommitBatch();
    }

    void LotusState::reconcileAbortedUinputReplacement() {
        if (!uinputAbortPending_) {
            return;
        }
        uinputAbortPending_ = false;

        // Stale abort: the context stayed away too long, likely a real application switch.
        if (uinputAbortTimeMs_ != 0 && nowMs() - uinputAbortTimeMs_ > 2000) {
            LOTUS_INFO("Drop the replacement aborted at focus out: it went stale");
            dropAbortedUinputStash();
            return;
        }

        if (stashedCommit_.empty()) {
            // Nothing to put back; the buffered keys replay as fresh input.
            dropAbortedUinputStash();
            return;
        }

        // The server-side Cancel finished the selection: the replacement text belongs at the cursor.
        appendCommit(stashedCommit_);
        oldPreBuffer_ = stashedNewWord_;
        hasHistory_   = !stashedNewWord_.empty();
        deferCommits_ = getFrontendName(&ic_) == "dbus";
        if (!bufferedKeys_.empty()) {
            replayBufferedKeys();
        }
        flushCommitBatch();
        LOTUS_INFO("Commit the replacement text aborted at focus out");
    }

    bool LotusState::isAutofillCertain(const SurroundingText& s) {
        if (!s.isValid() || oldPreBuffer_.empty() || isChromium_) {
            return false;
        }
        auto               textRange = fcitx::utf8::MakeUTF8CharRange(s.text());
        std::u32string     u32Text(textRange.begin(), textRange.end());

        const unsigned int cursor  = s.cursor();
        const unsigned int anchor  = s.anchor();
        const size_t       textLen = u32Text.length();

        // Surrounding text can lag behind IME state, so confirm oldPreBuffer_ is still near the cursor before trusting it.
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
            g_realTextLen.store(textLen, std::memory_order_release);
            return false;
        }

        // Heuristic: rapid text growth in a single-line context.
        // Applied only when no newline is present after the cursor to distinguish from AI text in editors.
        // Gecko/Firefox: if buffLen > textLen, surrounding text is stale (async update race)
        if (buffLen > textLen) {
            return false;
        }
        if (textLen > static_cast<size_t>(cursor) + 1 && cursor == g_realTextLen.load(std::memory_order_acquire) && u32Text.find(U'\n', cursor) == std::u32string::npos &&
            sameprefix)
            return true;

        for (auto v = g_realTextLen.load(std::memory_order_acquire); v < cursor && !g_realTextLen.compare_exchange_weak(v, cursor, std::memory_order_acq_rel);)
            ;
        return false;
    }

    void LotusState::handlePreeditMode(KeyEvent& keyEvent, KeySym currentSym) {
        const uint32_t keyVal = engineKeyValue(currentSym);
        if (lotusEngine_->processKey(keyVal, lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states())))) {
            keyEvent.filterAndAccept();
        } else {
            LOTUS_DEBUG("Preedit: key not processed, forwarding (sym=" + std::to_string(currentSym) + ", keyVal=" + std::to_string(keyVal) + ")");
        }
        const std::u32string commitText = pullCommitString();
        lotus::fcitx_text::commit(&ic_, commitText, lastCommitTime_);
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
                    lotus::fcitx_text::commit(&ic_, emojiBuffer_, lastCommitTime_);
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

    void LotusState::finishReplacement(KeyEvent& event) {
        appendCommit(pendingCommitString_);
        pendingCommitString_.clear();
        deferCommits_          = getFrontendName(&ic_) == "dbus";
        expectedBackspaces_    = 0;
        currentBackspaceCount_ = 0;
        replacementDeadlineMs_ = 0;
        realSelect_            = false;
        event.filterAndAccept();
        g_isDeleting.store(false, std::memory_order_release);
        if (deferCommits_) {
            flushCommitBatch();
        } else {
            replayBufferedKeys();
            flushCommitBatch();
        }
    }

    bool LotusState::handleUInputKeyPress(KeyEvent& event, KeySym currentSym) {
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return false;
        }
        // In Select mode the uinput server emits Shift+Left, so the echoed Left
        // presses are the selection signal. The final one is swallowed in
        // finishReplacement so the app only sees the count it should select.
        if (!isBackspace(currentSym) && (!realSelect_ || currentSym != FcitxKey_Left) && !(currentSym == FcitxKey_Pause)) {
            return false;
        }
        currentBackspaceCount_ += 1;
        if (currentBackspaceCount_ <= expectedBackspaces_ && currentSym != FcitxKey_Pause) {
            // Re-arm the deadline so a slow burst is never abandoned.
            replacementDeadlineMs_ = nowMs() + replacementEchoGrace_;
            return false; // Allow intermediate backspaces/lefts to reach the app to clear autofill/old text / extend the selection.
        }
        // The anchor closes the burst even if echoes are lost.
        finishReplacement(event);
        return true;
    }

    void LotusState::performReplacement(DiffPart diffPart) {
        // WA for libreoffice (issue #162)
        // force use deleteSurroundingText func
        if (isLibreoffice_) {
            const int delSize = static_cast<int>(diffPart.deletedPart.size());
            if (delSize > 0) {
                ic_.deleteSurroundingText(-delSize, delSize);
            }
            lotus::fcitx_text::commit(&ic_, diffPart.addedPart, lastCommitTime_);
            return;
        }
        const int  commitMs = clampDelay(appRuleSetting_.commitInterval > 0 ? appRuleSetting_.commitInterval : *engine_.config().defaultCommitInterval);
        const auto interval =
            static_cast<uint32_t>(clampDelay(appRuleSetting_.backspaceInterval > 0 ? appRuleSetting_.backspaceInterval : *engine_.config().defaultBackspaceInterval));
        uint32_t preDelay = 0;
        if (lastCommitTime_.time_since_epoch().count() > 0) {
            auto now       = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCommitTime_).count();
            if (elapsedMs < commitMs) {
                preDelay = static_cast<uint32_t>(commitMs - elapsedMs);
            }
        }
        LOTUS_INFO("Perform replacement: " + lotus::utf8::encode(diffPart.deletedPart) + " -> " + lotus::utf8::encode(diffPart.addedPart)); //NOLINT
        currentBackspaceCount_         = 0;
        pendingCommitString_           = diffPart.addedPart;
        expectedBackspaces_            = diffPart.deletedPart.size();
        const auto& surrounding        = ic_.surroundingText();
        bool        isAutofillCertain_ = isAutofillCertain(surrounding);
        if (currentMode() != LotusMode::SuperSmooth && isAutofillCertain_) {
            ++expectedBackspaces_;
        }
        const bool addressBar = isChromium_ && ic_.capabilityFlags().test(CapabilityFlag::Url);
        realSelect_           = (currentMode() == LotusMode::Select && !isAutofillCertain_) || (isUinputMode(currentMode()) && addressBar);
        lastSelect_           = realSelect_;
        g_isDeleting.store(true, std::memory_order_release);
        const auto    postDelay = static_cast<uint32_t>(clampDelay(appRuleSetting_.postDelay > 0 ? appRuleSetting_.postDelay : *engine_.config().defaultPostDelay));

        const int64_t injectMs = static_cast<int64_t>(expectedBackspaces_) * static_cast<int64_t>(interval);
        replacementDeadlineMs_ = nowMs() + static_cast<int64_t>(preDelay) + injectMs + static_cast<int64_t>(postDelay) + kReplacementAckSlackMs;
        replacementEchoGrace_  = static_cast<int64_t>(interval) + static_cast<int64_t>(postDelay) + kReplacementAckSlackMs;
        // Select mode: the uinput server selects with Shift+Left instead. The
        // trigger-key compensation above still applies because the last echoed
        // Left is swallowed; autofill compensation does not, see above.
        if (realSelect_) {
            sendSelectUinput(expectedBackspaces_, preDelay, interval, postDelay);
            LOTUS_INFO("Send select of " + std::to_string(expectedBackspaces_) + " characters");
            return;
        }
        sendBackspaceUinput(expectedBackspaces_, preDelay, interval, postDelay);
        LOTUS_INFO("Send " + std::to_string(expectedBackspaces_) + " backspaces");
    }

    bool LotusState::checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym) {
        if (keyEvent.key().isCursorMove() || currentSym == FcitxKey_Tab || currentSym == FcitxKey_KP_Tab || currentSym == FcitxKey_ISO_Left_Tab || currentSym == FcitxKey_Escape ||
            keyEvent.key().hasModifier()) {
            g_isDeleting.store(false, std::memory_order_release);
            expectedBackspaces_    = 0;
            currentBackspaceCount_ = 0;
            replacementDeadlineMs_ = 0;
            pendingCommitString_.clear();
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
        // Only used by optional double-space and double-hyphen replacements.
        // Both currently delete exactly one code point; normal typing uses
        // separate mode-specific handlers.
        if (currentMode() == LotusMode::SurroundingText) {
            ic_.deleteSurroundingText(-1, 1);
            lotus::fcitx_text::commit(&ic_, diffPart.addedPart, lastCommitTime_);
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

        if (g_uinputClientFd.load(std::memory_order_acquire) < 0) {
            setupUinput();
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

        // One restart pass: a key that invalidates an already committed syllable makes the
        // fallback restore the raw key strokes, which must not rewrite the committed word.
        for (int pass = 0;; ++pass) {
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
                        lotus::fcitx_text::commit(&ic_, diffPart.addedPart, lastCommitTime_);
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
            g_realTextLen.fetch_add(1, std::memory_order_acq_rel);

            const std::u32string preeditStr = pullPreeditString();

            DiffPart             diffPart = compareAndSplitU32(oldPreBuffer_, preeditStr);
            if (diffPart.deletedPart.empty()) {
                bool isCommit           = false;
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!diffPart.addedPart.empty()) {
                    oldPreBuffer_ = preeditStr;
                    if (wasAutoCapitalized || diffPart.addedPart != keyText) {
                        lotus::fcitx_text::commit(&ic_, diffPart.addedPart, lastCommitTime_);
                        keyEvent.filterAndAccept();
                        isCommit = true;
                    }
                }
                if (!isCommit) {
                    keyEvent.forward();
                }
                return;
            }

            // The committed word was transformed and the continuation invalidates it: keep the
            // committed word and restart the word from this key.
            if (pass == 0 && lotusEngine_->isNonVnRestore()) {
                hasHistory_ = false;
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
                continue;
            }

            if (g_uinputClientFd.load(std::memory_order_acquire) < 0) {
                LOTUS_ERROR("Cannot connect to uinput server, commit rawkey");
                std::string rawKey = keyEvent.key().toString();
                if (!rawKey.empty()) {
                    ic_.commitString(rawKey);
                }
                return;
            }

            if (g_isDeleting.load(std::memory_order_acquire)) {
                g_isDeleting.store(false, std::memory_order_release);
            }

            keyEvent.filterAndAccept();
            performReplacement(diffPart);
            oldPreBuffer_ = preeditStr;
            return;
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
            // The whole surrounding text is never decoded: the byte scan stops after kMaxScanLength
            // code points before the cursor, and only that slice is decoded for the engine.
            auto startIter = utf8::nextNChar(text.begin(), cursor);
            auto endIter   = startIter;

            int  scanCount = 0;
            while (startIter != text.begin() && scanCount < kMaxScanLength) {
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
                    lotus::fcitx_text::commit(ic, diffPart.addedPart, lastCommitTime_);
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
                lotus::fcitx_text::commit(ic, out, lastCommitTime_);
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

        if (g_uinputClientFd.load(std::memory_order_acquire) < 0) {
            ensureUinputServerConnected();
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

            bool isMacroExpansion = false;
            if (keyText.empty()) {
                isMacroExpansion = (commitStr != oldPreBuffer_);
            } else {
                isMacroExpansion = (commitStr != oldPreBuffer_ + keyText);
            }

            if (isMacroExpansion) {
                LOTUS_INFO("Macro expansion: '" + lotus::utf8::encode(oldPreBuffer_) + "' -> '" + lotus::utf8::encode(commitStr) + "'");
                // Try uinput replacement first, fallback to deleteSurroundingText, then plain commit
                if (g_uinputClientFd.load(std::memory_order_acquire) >= 0 && !oldPreBuffer_.empty()) {
                    performReplacement({.deletedPart = oldPreBuffer_, .addedPart = commitStr});
                } else if (ic_.capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                    const auto& surrounding = ic_.surroundingText();
                    if (surrounding.isValid()) {
                        const size_t oldLen = oldPreBuffer_.size();
                        if (oldLen > 0) {
                            ic_.deleteSurroundingText(-static_cast<int>(oldLen), static_cast<int>(oldLen));
                        }
                        lotus::fcitx_text::commit(&ic_, commitStr, lastCommitTime_);
                    } else {
                        lotus::fcitx_text::commit(&ic_, commitStr, lastCommitTime_);
                    }
                } else {
                    lotus::fcitx_text::commit(&ic_, commitStr, lastCommitTime_);
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
        if (currentMode() == LotusMode::Preedit) {
            if (keyEvent.rawKey().check(FcitxKey_Shift_L) || keyEvent.rawKey().check(FcitxKey_Shift_R))
                return true;
        } else {
            if (keyEvent.rawKey().isModifier() && !g_isDeleting.load(std::memory_order_acquire)) {
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
        trackShiftModifier(keyEvent);
        if (handleKeyEventGuards(keyEvent))
            return;
        ensureUinputConnection();
        completePendingReplacement();
        finishOverdueReplacement();

        if (g_mouseClicked.load(std::memory_order_acquire) && !g_isDeleting.load(std::memory_order_acquire)) {
            g_mouseClicked.store(false, std::memory_order_release);
            clearAllBuffers();
        }
        reconcileAbortedUinputReplacement();
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
        if (g_uinputClientFd.load(std::memory_order_acquire) < 0) {
            LOTUS_WARN("Cannot connect to uinput server, reconnecting....");
            ensureUinputServerConnected();
        }
    }

    void LotusState::completePendingReplacement() {
        if (currentBackspaceCount_ > expectedBackspaces_ && g_isDeleting.load(std::memory_order_acquire)) {
            g_isDeleting.store(false, std::memory_order_release);
            currentBackspaceCount_ = 0;
            expectedBackspaces_    = 0;
            replacementDeadlineMs_ = 0;
            realSelect_            = false;
            if (!bufferedKeys_.empty()) {
                deferCommits_ = getFrontendName(&ic_) == "dbus";
                replayBufferedKeys();
                flushCommitBatch();
            }
        }
    }

    void LotusState::finishOverdueReplacement() {
        if (replacementDeadlineMs_ == 0 || nowMs() < replacementDeadlineMs_) {
            return;
        }
        replacementDeadlineMs_ = 0;
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }

        cancelPendingUinput();
        LOTUS_WARN("Replacement ack timed out, finishing without the remaining backspace echoes");
        appendCommit(pendingCommitString_);
        pendingCommitString_.clear();
        expectedBackspaces_    = 0;
        currentBackspaceCount_ = 0;
        deletionInterruptedAt_ = 0;
        realSelect_            = false;
        g_isDeleting.store(false, std::memory_order_release);

        (*lotusEngine_).reset();
        oldPreBuffer_.clear();
        hasHistory_   = false;
        deferCommits_ = getFrontendName(&ic_) == "dbus";
        if (deferCommits_) {
            flushCommitBatch();
        } else {
            replayBufferedKeys();
            flushCommitBatch();
        }
    }

    void LotusState::applyAutoCapitalize(KeyEvent& keyEvent, KeySym& currentSym) {
        if (*engine_.config().autoCapitalizeAfterPunctuation && currentMode() != LotusMode::Off) {
            // Shortcuts must not change sentence capitalization state.
            // Match the modifier handling in checkForwardSpecialKey().
            if (keyEvent.key().hasModifier()) {
                return;
            }
            // Ignore auto-capitalize side-effects if we're processing automated replacement backspaces
            bool isAutomatedBackspace = (g_isDeleting.load(std::memory_order_acquire) && isBackspace(currentSym)) || currentSym == FcitxKey_Pause;

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
        if (g_isDeleting.load(std::memory_order_acquire)) {
            if (isBackspace(currentSym)) {
                if (g_realTextLen.load(std::memory_order_acquire) > 0)
                    g_realTextLen.fetch_sub(1, std::memory_order_acq_rel);
                if (handleUInputKeyPress(keyEvent, currentSym)) {
                    return true;
                }
            } else if (currentSym == FcitxKey_Pause) {
                handleUInputKeyPress(keyEvent, currentSym);
                return true;
            } else if (realSelect_ && (currentSym == FcitxKey_Left || currentSym == FcitxKey_Delete)) {
                if (currentSym == FcitxKey_Left) {
                    // Echoed left arrow extends the selection in the app: consume it as
                    // a selection signal, never buffer it for replay.
                    if (g_realTextLen.load(std::memory_order_acquire) > 0)
                        g_realTextLen.fetch_sub(1, std::memory_order_acq_rel);
                    handleUInputKeyPress(keyEvent, currentSym);
                } else {
                    keyEvent.forward();
                }
                return true;
            } else {
                // Buffer incoming keys during the short uinput replacement
                // burst to prevent them from interfering with backspaces
                // and the pending commit. Some non-text keys may be consumed
                // during this window. Forwarding them immediately could change
                // the cursor before replacement completes. This is a known
                // limitation of the fast-typing buffer.
                // The modifier is not decided here: at press time the echo counter
                // is a racy proxy for the uinput server's Shift hold in Select
                // mode. The raw event is buffered and the modifier is chosen when
                // the key is popped for replay, see replayBufferedKeys().
                KeySym      symToBuffer   = currentSym;
                KeyStates   stateToBuffer = keyEvent.rawKey().states();
                std::string keyUtf8Check  = Key::keySymToUTF8(symToBuffer);
                if (!keyUtf8Check.empty()) {
                    LOTUS_WARN("Typing so fast, add key to queue");
                    bufferedKeys_.push({.sym = symToBuffer, .state = stateToBuffer});
                }
                keyEvent.filterAndAccept();
            }
            return true;
        }
        return false;
    }

    bool LotusState::handleDoubleSpaceToPeriod(KeyEvent& keyEvent, KeySym currentSym) {
        if (*engine_.config().doubleSpaceToPeriod && currentMode() != LotusMode::Off) {
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
        if (*engine_.config().doubleHyphenToEmDash && currentMode() != LotusMode::Off) {
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
        switch (currentMode()) {
            case LotusMode::Smooth:
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
        if (isFocusOut && isUinputMode(currentMode())) {
            deferredFocusOut_ = true;
            deliverPendingCommits();
            resetMacroSkip();
            ic_.inputPanel().reset();
            return;
        }
        deferredFocusOut_ = false;
        resetNow(isFocusOut);
    }

    void LotusState::resetNow(bool isFocusOut) {
        const auto& surrounding = ic_.surroundingText();
        const auto& text        = surrounding.text();
        size_t      textLen     = utf8::length(text);
        g_realTextLen.store(textLen, std::memory_order_release);
        if (g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        deliverPendingCommits();
        resetMacroSkip();

        if (lotusEngine_) {
            isPrevSpace_  = false;
            isPrevHyphen_ = false;
            if (isFocusOut) {
                shouldCapitalize_  = false;
                isPrevPunctuation_ = false;
            }
            (*lotusEngine_).reset();
            oldPreBuffer_.clear();
            hasHistory_ = false;
        }
        if (getFrontendName(&ic_) != "dbus")
            clearAllBuffers(!isFocusOut);

        switch (currentMode()) {
            case LotusMode::Preedit: {
                ic_.inputPanel().reset();
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_.updatePreedit();
                break;
            }
            case LotusMode::SurroundingText:
            case LotusMode::Smooth:
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

    void LotusState::flushDeferredFocusOut() {
        if (!deferredFocusOut_) {
            return;
        }
        deferredFocusOut_ = false;
        resetNow(true);
    }

    void LotusState::cancelDeferredFocusOut() {
        deferredFocusOut_ = false;
    }

    void LotusState::commitBuffer() {
        deliverPendingCommits();
        switch (currentMode()) {
            case LotusMode::Preedit: {
                ic_.inputPanel().reset();
                ic_.inputPanel().setClientPreedit(Text());
                ic_.inputPanel().setPreedit(Text());
                ic_.updatePreedit();
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                if (lotusEngine_) {
                    (*lotusEngine_).commitPreedit();
                    const std::u32string commitText = pullCommitString();
                    lotus::fcitx_text::commit(&ic_, commitText, lastCommitTime_);
                    (*lotusEngine_).reset();
                }
                ic_.updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_.updatePreedit();
                break;
            }
            case LotusMode::Smooth:
            case LotusMode::SurroundingText:
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

    void LotusState::clearAllBuffers(bool keepSentenceState) {
        deliverPendingCommits();
        LOTUS_DEBUG("Clear all buffers");
        if (g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        resetMacroSkip();
        oldPreBuffer_.clear();
        hasHistory_         = false;
        uinputAbortPending_ = false;
        stashedCommit_.clear();
        stashedNewWord_.clear();
        uinputAbortTimeMs_ = 0;
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            expectedBackspaces_    = 0;
            currentBackspaceCount_ = 0;
            replacementDeadlineMs_ = 0;
            pendingCommitString_.clear();
        }
        emojiBuffer_.clear();
        emojiCandidates_.clear();
        bufferedKeys_.clear();
        if (!keepSentenceState) {
            shouldCapitalize_ = false;
        }
        isPrevSpace_  = false;
        isPrevHyphen_ = false;
        if (!keepSentenceState) {
            isPrevPunctuation_ = false;
        }
        if (lotusEngine_)
            (*lotusEngine_).reset();
    }

    bool LotusState::isEmptyHistory() const {
        return !hasHistory_;
    }

    void LotusState::appendCommit(std::u32string_view text) {
        commitBatch_.append(text);
    }

    void LotusState::scheduleDeferredCommit() {
        if (deferredCommitPending_) {
            return;
        }
        deferredCommitPending_ = true;
        deferredCommitTimer_   = engine_.instance()->eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), 0, [this](EventSourceTime*, uint64_t) {
            deferredCommitPending_ = false;
            deferCommits_          = false;
            commitBatch_           = std::move(deferredCommitText_);
            deferredCommitText_.clear();
            replayBufferedKeys();
            flushCommitBatch();
            return false;
        });
    }

    void LotusState::flushCommitBatch() {
        if (commitBatch_.empty()) {
            return;
        }
        if (deferCommits_) {
            deferredCommitText_ += commitBatch_;
            commitBatch_.clear();
            scheduleDeferredCommit();
            return;
        }
        lotus::fcitx_text::commit(&ic_, commitBatch_, lastCommitTime_);
        commitBatch_.clear();
    }

    void LotusState::deliverPendingCommits() {
        deferCommits_ = false;
        if (!deferredCommitText_.empty()) {
            commitBatch_ += deferredCommitText_;
            deferredCommitText_.clear();
            deferredCommitPending_ = false;
        }
        flushCommitBatch();
    }

    void LotusState::replayBufferedKeys() {
        const bool stripSelectShift = lastSelect_;
        LOTUS_INFO("Starting replay buffered keys");
        while (!bufferedKeys_.empty()) {
            const KeyEntry entry = bufferedKeys_.pop();
            auto           sym   = static_cast<KeySym>(entry.sym);
            KeyStates      state(entry.state);
            // The modifier is chosen at pop time, not at press time. Replay
            // runs after the replacement burst finished, so the uinput server
            // has already released the synthetic Shift it held for the
            // Shift+Left selection. A stored Shift bit is therefore stripped
            // unless Shift is still physically held right now (tracked live
            // from key events), which means the user is holding it for a
            // genuine capital letter. Backspace-driven modes never touch
            // Shift, so their state is replayed untouched.
            if (stripSelectShift && state.test(KeyState::Shift) && !isShiftHeld()) {
                sym   = unshiftKeySym(sym);
                state = state.unset(KeyState::Shift);
            }
            const uint32_t keyCodePoint = Key::keySymToUnicode(sym);
            if (keyCodePoint == 0U) {
                continue;
            }
            const std::u32string keyText(1, static_cast<char32_t>(keyCodePoint));

            // Same single restart pass as handleUinputMode: a replayed key that invalidates the
            // committed syllable must not rewrite the committed word with raw key strokes.
            bool replacementStarted = false;
            for (int pass = 0;; ++pass) {
                bool                 processed = lotusEngine_->processKey(engineKeyValue(sym), lotus::keyStatesFromBits(state.toInteger()));

                const std::u32string commitStr = pullCommitString();
                if (!commitStr.empty()) {
                    DiffPart diffPart = compareAndSplitU32(oldPreBuffer_, commitStr);

                    if (!diffPart.deletedPart.empty()) {
                        flushCommitBatch();
                        // Remaining keys stay queued for the next replay cycle.
                        performReplacement(diffPart);
                        hasHistory_ = false;
                        (*lotusEngine_).reset();
                        oldPreBuffer_.clear();
                        replacementStarted = true;
                        break;
                    }
                    if (!diffPart.addedPart.empty()) {
                        appendCommit(diffPart.addedPart);
                    }

                    hasHistory_ = false;
                    (*lotusEngine_).reset();
                    oldPreBuffer_.clear();
                    break;
                }

                if (!processed) {
                    appendCommit(keyText);
                    break;
                }

                hasHistory_ = true;
                g_realTextLen.fetch_add(1, std::memory_order_acq_rel);

                const std::u32string preeditStr = pullPreeditString();

                DiffPart             diffPart = compareAndSplitU32(oldPreBuffer_, preeditStr);
                if (diffPart.deletedPart.empty()) {
                    if (!diffPart.addedPart.empty()) {
                        appendCommit(diffPart.addedPart);
                        oldPreBuffer_ = preeditStr;
                    }
                    break;
                }

                if (pass == 0 && lotusEngine_->isNonVnRestore()) {
                    hasHistory_ = false;
                    (*lotusEngine_).reset();
                    oldPreBuffer_.clear();
                    continue;
                }

                if (g_uinputClientFd.load(std::memory_order_acquire) < 0) {
                    appendCommit(keyText);
                    break;
                }

                if (g_isDeleting.load(std::memory_order_acquire)) {
                    g_isDeleting.store(false, std::memory_order_release);
                }

                flushCommitBatch();
                // Remaining keys stay queued for the next replay cycle.
                performReplacement(diffPart);
                oldPreBuffer_      = preeditStr;
                replacementStarted = true;
                break;
            }
            if (replacementStarted) {
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
            trackingModifierTap_ = false;
            return;
        }
        if (keyEvent.isRelease()) {
            if (trackingModifierTap_) {
                trackingModifierTap_ = false;
                macroSkip_           = true;
                lotusEngine_->setMacroEnabled(false);
                LOTUS_INFO("Macro skip enabled for next word");
            }
        } else {
            trackingModifierTap_ = true;
        }
    }

    void LotusState::cancelModifierTap() {
        trackingModifierTap_ = false;
    }

    void LotusState::trackShiftModifier(const KeyEvent& keyEvent) {
        const KeySym sym = keyEvent.rawKey().sym();
        if (sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) {
            shiftHeld_ = !keyEvent.isRelease();
        }
    }

    bool LotusState::isShiftHeld() const {
        return shiftHeld_;
    }

    void LotusState::forgetShiftState() {
        shiftHeld_ = false;
    }

    void LotusState::reEnableMacroAfterWordEnd() {
        if (!macroSkip_) {
            return;
        }
        if (!pullPreeditString().empty()) {
            return;
        }
        macroSkip_ = false;
        lotusEngine_->setMacroEnabled(*engine_.config().enableMacro);
    }

    void LotusState::resetMacroSkip() {
        trackingModifierTap_ = false;
        macroSkip_           = false;
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
