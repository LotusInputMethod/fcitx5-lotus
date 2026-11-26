/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-state.h"
#include "lotus-engine.h"
#include "lotus-candidates.h"
#include "lotus-fcitx-text.h"
#include "lotus-log.h"
#include "lotus-utils.h"
#include "lotus-wlr-virtual-keyboard.h"
#include "lotus-x11-keyboard.h"

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
    constexpr int     kMaxScanLength              = 15;
    constexpr int64_t kReplacementAckSlackMs      = 200;
    constexpr int64_t kMouseClickWatchdogMs       = 200; ///< Time a client gets to reset itself after a click
    constexpr int64_t kMinEchoCommitOrderWindowMs = 50;  ///< Keep printable keys on D-Bus right after commitString so forward() cannot overtake it

    constexpr int     kMinDelayMs = 1;
    constexpr int     kMaxDelayMs = 1000;

    int               clampDelay(int value) {
        return std::clamp(value, kMinDelayMs, kMaxDelayMs);
    }

    bool isWordBreak(uint32_t ucs4) {
        // Space, tab, newline, carriage return, null, or punctuation/symbols (: ; < = > ? @)
        return ucs4 == ' ' || ucs4 == '\t' || ucs4 == '\n' || ucs4 == '\r' || ucs4 == 0 || (ucs4 >= 58 && ucs4 <= 64);
    }

    bool isEchoFrontend(std::string_view frontend) {
        return frontend == "dbus" || frontend == "fcitx4" || frontend == "x11" || frontend == "xim";
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

    bool LotusState::vkUsable() const {
        return *engine_.config().preferVirtualKeyboard && LotusWlrVirtualKeyboard::instance().available();
    }

    bool LotusState::x11Usable() const {
        return *engine_.config().preferVirtualKeyboard && getEnv("WAYLAND_DISPLAY").empty() && LotusX11Keyboard::instance().available();
    }

    bool LotusState::injectionUsable() const {
        return vkUsable() || x11Usable();
    }

    bool LotusState::setupUinput() {
        return engine_.kbSender().ensureConnected();
    }

    void LotusState::sendKbMsg(KbOp op, size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay) {
        const KbMsg msg{.op = op, .count = count, .preDelay = preDelay, .interval = interval, .postDelay = postDelay};
        engine_.kbSender().send(msg);
    }

    void LotusState::sendBackspaceUinput(size_t count, uint32_t preDelay, uint32_t interval, uint32_t postDelay) {
        sendKbMsg(KbOp::Backspace, count, preDelay, interval, postDelay);
    }

    void LotusState::cancelPendingUinput() {
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        sendKbMsg(KbOp::Cancel, 0, 0, 0, 0);
        LOTUS_INFO("Cancel the burst still pending on the uinput server");
    }

    void LotusState::discardForAppChange() {
        cancelPendingUinput();
        g_isDeleting.store(false, std::memory_order_release);
        deferredCommitTimer_.reset();
        deferredCommitPending_ = false;
        deferredCommitText_.clear();
        commitBatch_.clear();
        deferredFocusOut_ = false;
        clearAllBuffers();
    }

    void LotusState::armMouseClickWatchdog() {
        if (currentMode() != LotusMode::Preedit || mouseClickWatchdog_) {
            return;
        }
        mouseClickWatchdog_ =
            engine_.instance()->eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + (kMouseClickWatchdogMs * 1000), 0, [this](EventSourceTime*, uint64_t) {
                [[maybe_unused]] auto* released = mouseClickWatchdog_.release();
                if (currentMode() == LotusMode::Preedit && !pullPreeditString().empty()) {
                    LOTUS_INFO("Mouse click watchdog: drop the preedit the client already committed");
                    reset();
                }
                return false;
            });
        LOTUS_DEBUG("Mouse click watchdog armed");
    }

    void LotusState::cancelMouseClickWatchdog() {
        mouseClickWatchdog_.reset();
    }

    void LotusState::rearmReplacementDeadline() {
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        const auto remaining   = static_cast<int64_t>(expectedBackspaces_ - std::min(expectedBackspaces_, currentBackspaceCount_));
        replacementDeadlineMs_ = nowMs() + (remaining * static_cast<int64_t>(burstIntervalMs_)) + replacementEchoGrace_;
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
        event.filterAndAccept();
        finishReplacementNow();
    }

    void LotusState::finishReplacementNow() {
        ++replacementToken_;
        appendCommit(pendingCommitString_);
        pendingCommitString_.clear();
        deferCommits_          = getFrontendName(&ic_) == "dbus";
        expectedBackspaces_    = 0;
        currentBackspaceCount_ = 0;
        replacementDeadlineMs_ = 0;
        g_isDeleting.store(false, std::memory_order_release);
        if (deferCommits_) {
            flushCommitBatch();
        } else {
            replayBufferedKeys();
            flushCommitBatch();
        }
    }

    void LotusState::finishScheduledReplacement(uint64_t token) {
        if (token != replacementToken_ || !g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        finishReplacementNow();
    }

    bool LotusState::handleUInputKeyPress(KeyEvent& event, KeySym currentSym) {
        if (!g_isDeleting.load(std::memory_order_acquire)) {
            return false;
        }
        if (!isBackspace(currentSym) && currentSym != FcitxKey_Pause) {
            return false;
        }
        if (currentSym != FcitxKey_Pause) {
            if (currentBackspaceCount_ < expectedBackspaces_) {
                currentBackspaceCount_ += 1;
            }
            // Re-arm the deadline so a slow burst is never abandoned.
            replacementDeadlineMs_ = nowMs() + replacementEchoGrace_;
            // Injected backend: the last echoed backspace is the trigger, commit on it.
            const bool injectedEcho = echoTriggerPending_ && isBackspace(currentSym);
            if (injectedEcho && currentBackspaceCount_ >= expectedBackspaces_) {
                echoTriggerPending_ = false;
                // Let the app finish the backspaces and the focus bounce before
                // commit. Consume the echoed trigger now and let the backend
                // worker wait the post-delay, so the fcitx main thread keeps
                // processing key presses while it elapses.
                const int postMs = clampDelay(appRuleSetting_.postDelay > 0 ? appRuleSetting_.postDelay : *engine_.config().defaultPostDelay);
                event.filterAndAccept();
                const uint64_t token  = replacementToken_;
                auto           icRef  = ic_.watch();
                LotusEngine*   engine = &engine_;
                scheduleAfterInjection(postMs, [engine, icRef, token] {
                    engine->scheduleOnMainThread([engine, icRef, token] {
                        if (auto* state = engine->stateFor(icRef.get())) {
                            state->finishScheduledReplacement(token);
                        }
                    });
                });
                return true;
            }
            return false; // Allow intermediate backspaces to reach the app to clear the old text.
        }
        // The anchor closes the burst even if echoes are lost.
        currentBackspaceCount_ += 1;
        finishReplacement(event);
        return true;
    }
    uint32_t LotusState::computeCommitPreDelay(int commitMs) const {
        const bool commitPending = deferredCommitPending_ || !deferredCommitText_.empty() || !commitBatch_.empty();
        uint32_t   preDelay      = 0;
        if (lastCommitTime_.time_since_epoch().count() > 0) {
            const auto now = std::chrono::steady_clock::now();
            if (const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCommitTime_).count(); elapsedMs < commitMs) {
                preDelay = static_cast<uint32_t>(commitMs - elapsedMs);
            }
        }
        if (commitPending) {
            preDelay = std::max<uint32_t>(preDelay, static_cast<uint32_t>(commitMs));
        }
        return preDelay;
    }

    bool LotusState::isRecentEchoCommit() const {
        if (!isEchoFrontend(getFrontendName(&ic_)) || lastCommitTime_.time_since_epoch().count() <= 0) {
            return false;
        }
        const int     commitMs = clampDelay(appRuleSetting_.commitInterval > 0 ? appRuleSetting_.commitInterval : *engine_.config().defaultCommitInterval);
        const int64_t windowMs = std::max<int64_t>(commitMs, kMinEchoCommitOrderWindowMs);
        const auto    elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastCommitTime_).count();
        return elapsed < windowMs;
    }

    void LotusState::commitOrInjectText(std::u32string_view text) {
        if (text.empty()) {
            return;
        }
        if (vkUsable() && !isEchoFrontend(getFrontendName(&ic_))) {
            auto&             wlr       = LotusWlrVirtualKeyboard::instance();
            const std::string addedUtf8 = lotus::utf8::encode(text);
            const int         commitMs  = clampDelay(appRuleSetting_.commitInterval > 0 ? appRuleSetting_.commitInterval : *engine_.config().defaultCommitInterval);
            const uint32_t    preDelay  = computeCommitPreDelay(commitMs);
            auto              emitNow   = [&wlr, addedUtf8] { return wlr.emit(0, addedUtf8, true, false); };
            if (preDelay > 0) {
                wlr.runAfter(static_cast<int>(preDelay), emitNow);
                return;
            }
            if (emitNow()) {
                return;
            }
        }
        lotus::fcitx_text::commit(&ic_, text, lastCommitTime_);
    }
    void LotusState::performReplacement(DiffPart diffPart) {
        LOTUS_INFO("Perform replacement: " + lotus::utf8::encode(diffPart.deletedPart) + " -> " + lotus::utf8::encode(diffPart.addedPart)); //NOLINT
        echoTriggerPending_ = false;
        ++replacementToken_;
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

        const size_t delSize     = graphemeClusterCount(diffPart.deletedPart);
        const bool   dotAnchor   = currentMode() == LotusMode::Smooth;
        const size_t deleteCount = delSize + (dotAnchor ? 1U : 0U);
        const int    burstCount  = static_cast<int>(deleteCount);

        const int    commitMs = clampDelay(appRuleSetting_.commitInterval > 0 ? appRuleSetting_.commitInterval : *engine_.config().defaultCommitInterval);
        const auto   interval =
            static_cast<uint32_t>(clampDelay(appRuleSetting_.backspaceInterval > 0 ? appRuleSetting_.backspaceInterval : *engine_.config().defaultBackspaceInterval));
        const auto        postDelay = static_cast<uint32_t>(clampDelay(appRuleSetting_.postDelay > 0 ? appRuleSetting_.postDelay : *engine_.config().defaultPostDelay));

        uint32_t          preDelay     = computeCommitPreDelay(commitMs);
        const std::string frontend     = getFrontendName(&ic_);
        const bool        echoFrontend = isEchoFrontend(frontend);
        const std::string addedUtf8    = lotus::utf8::encode(diffPart.addedPart);
        const bool        x11Backend   = !vkUsable() && x11Usable();
        bool              echoDriven   = false;
        injectionBackend_              = InjectionBackend::None;

        if (vkUsable()) {
            echoDriven = echoFrontend;
            if (!echoDriven) {
                auto&             wlr     = LotusWlrVirtualKeyboard::instance();
                auto              emitNow = [&wlr, burstCount, addedUtf8, dotAnchor] { return wlr.emit(burstCount, addedUtf8, true, dotAnchor); };
                const std::string wlrLog  = "wlr-virtual-keyboard: sent " + std::to_string(burstCount) + " backspaces + keys for [" + addedUtf8 + "]";
                if (preDelay > 0) {
                    wlr.runAfter(static_cast<int>(preDelay), emitNow);
                    LOTUS_INFO(wlrLog);
                    return;
                }
                if (emitNow()) {
                    LOTUS_INFO(wlrLog);
                    return;
                }
                LOTUS_WARN("wlr-virtual-keyboard emit failed, fallback use uinput");
            } else {
                injectionBackend_ = InjectionBackend::WlrVirtualKeyboard;
            }
        } else if (x11Backend) {
            echoDriven = echoFrontend;
            if (!echoDriven) {
                auto&             x11     = LotusX11Keyboard::instance();
                auto              emitNow = [&x11, burstCount, addedUtf8, interval, dotAnchor] { return x11.emit(burstCount, addedUtf8, static_cast<int>(interval), dotAnchor); };
                const std::string x11Log  = "x11-keyboard: sent " + std::to_string(burstCount) + " backspaces + keys for [" + addedUtf8 + "]";
                if (preDelay > 0) {
                    x11.runAfter(static_cast<int>(preDelay), emitNow);
                    LOTUS_INFO(x11Log);
                    return;
                }
                if (emitNow()) {
                    LOTUS_INFO(x11Log);
                    return;
                }
                LOTUS_WARN("x11-keyboard emit failed, fallback use uinput");
            } else {
                injectionBackend_ = InjectionBackend::X11Test;
            }
        }

        if (dotAnchor) {
            preDelay = std::max<uint32_t>(preDelay, static_cast<uint32_t>(commitMs));
            lotus::fcitx_text::commit(&ic_, kDotAnchorText, lastCommitTime_);
        }

        currentBackspaceCount_ = 0;
        pendingCommitString_   = diffPart.addedPart;
        // Echo-driven backends: the extra last backspace is the trigger, it is swallowed and commits.
        expectedBackspaces_ = echoDriven ? deleteCount + 1U : deleteCount;
        g_isDeleting.store(true, std::memory_order_release);

        const int64_t injectMs = static_cast<int64_t>(expectedBackspaces_) * static_cast<int64_t>(interval);
        replacementDeadlineMs_ = nowMs() + static_cast<int64_t>(preDelay) + injectMs + static_cast<int64_t>(postDelay) + kReplacementAckSlackMs;
        replacementEchoGrace_  = static_cast<int64_t>(interval) + static_cast<int64_t>(postDelay) + kReplacementAckSlackMs;
        burstIntervalMs_       = interval;

        if (injectionBackend_ != InjectionBackend::None) {
            echoTriggerPending_ = true;
            if (preDelay > 0) {
                // The previous commit may still be in flight over dbus: wait before the backspaces.
                const uint64_t token  = replacementToken_;
                auto           icRef  = ic_.watch();
                LotusEngine*   engine = &engine_;
                const size_t   count  = expectedBackspaces_;
                scheduleAfterInjection(static_cast<int>(preDelay), [engine, icRef, token, count] {
                    engine->scheduleOnMainThread([engine, icRef, token, count] {
                        auto* state = engine->stateFor(icRef.get());
                        if (state == nullptr || token != state->replacementToken_ || !g_isDeleting.load(std::memory_order_acquire)) {
                            return;
                        }
                        if (!state->injectEchoBurst(count)) {
                            state->echoTriggerPending_ = false;
                            state->finishReplacementNow();
                        }
                    });
                });
                return;
            }
            if (injectEchoBurst(expectedBackspaces_)) {
                return;
            }
            LOTUS_WARN("injected burst failed, fallback use uinput");
            echoTriggerPending_ = false;
            injectionBackend_   = InjectionBackend::None;
            expectedBackspaces_ = deleteCount;
        }
        sendBackspaceUinput(expectedBackspaces_, preDelay, interval, postDelay);
        LOTUS_INFO("Send " + std::to_string(expectedBackspaces_) + " backspaces");
    }

    bool LotusState::injectEchoBurst(size_t count) {
        switch (injectionBackend_) {
            case InjectionBackend::WlrVirtualKeyboard: return LotusWlrVirtualKeyboard::instance().emit(static_cast<int>(count), "", false);
            case InjectionBackend::X11Test: return LotusX11Keyboard::instance().emit(static_cast<int>(count), "", static_cast<int>(burstIntervalMs_), false);
            case InjectionBackend::None: break;
        }
        return false;
    }

    void LotusState::scheduleAfterInjection(int delayMs, std::function<void()> task) {
        if (injectionBackend_ == InjectionBackend::X11Test) {
            LotusX11Keyboard::instance().runAfter(delayMs, std::move(task));
        } else {
            LotusWlrVirtualKeyboard::instance().runAfter(delayMs, std::move(task));
        }
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

        if (!injectionUsable() && !engine_.kbSender().connected()) {
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
        // Wayland keymap uploads reset the virtual keyboard's modifiers (including Shift and locked
        // CapsLock), so forwarded keys can lose uppercase state on the compositor/fcitx5 seat; and
        // on echo frontends (e.g. dbus), a fast forwarded key can overtake a just-sent commitString.
        // Deliver printable characters directly in both cases.
        const auto           rawStates               = keyEvent.rawKey().states();
        const bool           bypassForwardForCaseMod = vkUsable() && (isShiftHeld() || rawStates.test(KeyState::Shift) || rawStates.test(KeyState::CapsLock));
        const bool           deliverDirectly         = bypassForwardForCaseMod || isRecentEchoCommit();

        bool                 processed = lotusEngine_->processKey(engineKeyValue(currentSym), lotus::keyStatesFromBits(static_cast<uint32_t>(keyEvent.rawKey().states())));

        const std::u32string commitStr = pullCommitString();
        if (!commitStr.empty()) {
            DiffPart diffPart = compareAndSplitU32(oldPreBuffer_, commitStr);

            if (!diffPart.deletedPart.empty()) {
                performReplacement(diffPart);
                keyEvent.filterAndAccept();
            } else {
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!diffPart.addedPart.empty() && (keyText != diffPart.addedPart || wasAutoCapitalized || deliverDirectly)) {
                    if (deliverDirectly) {
                        commitOrInjectText(diffPart.addedPart);
                    } else {
                        lotus::fcitx_text::commit(&ic_, diffPart.addedPart, lastCommitTime_);
                    }
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
                if (deliverDirectly) {
                    commitOrInjectText(keyText);
                    keyEvent.filterAndAccept();
                } else {
                    keyEvent.forward();
                }
            }
            return;
        }

        hasHistory_ = true;

        const std::u32string preeditStr = pullPreeditString();

        DiffPart             diffPart = compareAndSplitU32(oldPreBuffer_, preeditStr);
        if (diffPart.deletedPart.empty()) {
            bool isCommit           = false;
            bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
            if (!diffPart.addedPart.empty()) {
                oldPreBuffer_ = preeditStr;
                if (wasAutoCapitalized || deliverDirectly || diffPart.addedPart != keyText) {
                    if (deliverDirectly) {
                        commitOrInjectText(diffPart.addedPart);
                    } else {
                        lotus::fcitx_text::commit(&ic_, diffPart.addedPart, lastCommitTime_);
                    }
                    keyEvent.filterAndAccept();
                    isCommit = true;
                }
            }
            if (!isCommit) {
                keyEvent.forward();
            }
            return;
        }

        if (!injectionUsable() && !engine_.kbSender().connected()) {
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
            // Only a pure append of the typed key can go straight to the client. When the whole old
            // word is replaced by the key ("ư" + "w" -> "w"), the old word must still be deleted.
            if (diffPart.deletedPart.empty() && diffPart.addedPart == lotus::utf8::decode(keyEvent.key().toString())) {
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

        if (!injectionUsable() && !engine_.kbSender().connected()) {
            engine_.kbSender().ensureConnected();
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
                if ((injectionUsable() || engine_.kbSender().connected()) && !oldPreBuffer_.empty()) {
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
        // Only the wlroots backend leaves echoes that have to be matched by keycode and forwarded. Xtest keys come
        // back through the XIM/dbus frontend like uinput keys and are counted by handleUInputKeyPress.
        if (injectionBackend_ != InjectionBackend::X11Test) {
            auto& vk = LotusWlrVirtualKeyboard::instance();
            if (vk.injecting()) {
                keyEvent.filterAndAccept();
                return true;
            }
            if (vk.consumeEcho(static_cast<uint32_t>(keyEvent.rawKey().code()), static_cast<uint32_t>(keyEvent.time()))) {
                keyEvent.forward();
                return true;
            }
        }
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
        if (!keyEvent.rawKey().isModifier()) {
            const auto rawStates = keyEvent.rawKey().states();
            LotusWlrVirtualKeyboard::instance().setUserModifiers(isShiftHeld() || rawStates.test(KeyState::Shift), rawStates.test(KeyState::CapsLock));
        }
        if (handleKeyEventGuards(keyEvent))
            return;

        if (deferredCommitPending_ || !deferredCommitText_.empty()) {
            const KeySym    bufferedSym   = keyEvent.rawKey().sym();
            const KeyStates bufferedState = keyEvent.rawKey().states();
            if (!Key::keySymToUTF8(bufferedSym).empty()) {
                bufferedKeys_.push({.sym = bufferedSym, .state = bufferedState});
                keyEvent.filterAndAccept();
                return;
            }
        }
        if (mouseClickWatchdog_) {
            cancelMouseClickWatchdog();
            if (currentMode() == LotusMode::Preedit && !pullPreeditString().empty()) {
                reset();
            }
        }
        ensureUinputConnection();
        completePendingReplacement();
        finishOverdueReplacement();

        if (g_mouseClicked.load(std::memory_order_acquire) && !g_isDeleting.load(std::memory_order_acquire)) {
            g_mouseClicked.store(false, std::memory_order_release);
            if (mouseClickAction(currentMode()) == MouseClickAction::ClearBuffers) {
                clearAllBuffers();
            }
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
        if (!injectionUsable() && !engine_.kbSender().connected()) {
            LOTUS_WARN("Cannot connect to uinput server, reconnecting....");
            engine_.kbSender().ensureConnected();
        }
    }

    void LotusState::completePendingReplacement() {
        if (currentBackspaceCount_ > expectedBackspaces_ && g_isDeleting.load(std::memory_order_acquire)) {
            g_isDeleting.store(false, std::memory_order_release);
            currentBackspaceCount_ = 0;
            expectedBackspaces_    = 0;
            replacementDeadlineMs_ = 0;
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
                if (handleUInputKeyPress(keyEvent, currentSym)) {
                    return true;
                }
            } else if (currentSym == FcitxKey_Pause) {
                handleUInputKeyPress(keyEvent, currentSym);
                return true;
            } else {
                // Buffer incoming keys during the short uinput replacement
                // burst to prevent them from interfering with backspaces
                // and the pending commit. Some non-text keys may be consumed
                // during this window. Forwarding them immediately could change
                // the cursor before replacement completes. This is a known
                // limitation of the fast-typing buffer.
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
            case LotusMode::SuperSmooth: {
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
        cancelMouseClickWatchdog();
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
            case LotusMode::SuperSmooth: {
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
            case LotusMode::SuperSmooth: {
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
        cancelMouseClickWatchdog();
        deliverPendingCommits();
        LOTUS_DEBUG("Clear all buffers");
        if (g_isDeleting.load(std::memory_order_acquire)) {
            return;
        }
        resetMacroSkip();
        oldPreBuffer_.clear();
        hasHistory_ = false;
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
        LOTUS_INFO("Starting replay buffered keys");
        while (!bufferedKeys_.empty()) {
            const KeyEntry entry = bufferedKeys_.pop();
            auto           sym   = static_cast<KeySym>(entry.sym);
            KeyStates      state(entry.state);
            const uint32_t keyCodePoint = Key::keySymToUnicode(sym);
            if (keyCodePoint == 0U) {
                continue;
            }
            const std::u32string keyText(1, static_cast<char32_t>(keyCodePoint));

            bool                 replacementStarted = false;
            bool                 processed          = lotusEngine_->processKey(engineKeyValue(sym), lotus::keyStatesFromBits(state.toInteger()));

            const std::u32string commitStr = pullCommitString();
            if (!commitStr.empty()) {
                DiffPart diffPart = compareAndSplitU32(oldPreBuffer_, commitStr);

                if (!diffPart.deletedPart.empty()) {
                    flushCommitBatch();
                    // Remaining keys stay queued for the next replay cycle.
                    performReplacement(diffPart);
                    replacementStarted = true;
                } else if (!diffPart.addedPart.empty()) {
                    appendCommit(diffPart.addedPart);
                }

                hasHistory_ = false;
                (*lotusEngine_).reset();
                oldPreBuffer_.clear();
            } else if (!processed) {
                appendCommit(keyText);
            } else {
                hasHistory_ = true;

                const std::u32string preeditStr = pullPreeditString();

                DiffPart             diffPart = compareAndSplitU32(oldPreBuffer_, preeditStr);
                if (diffPart.deletedPart.empty()) {
                    if (!diffPart.addedPart.empty()) {
                        appendCommit(diffPart.addedPart);
                        oldPreBuffer_ = preeditStr;
                    }
                } else if (!injectionUsable() && !engine_.kbSender().connected()) {
                    appendCommit(keyText);
                } else {
                    if (g_isDeleting.load(std::memory_order_acquire)) {
                        g_isDeleting.store(false, std::memory_order_release);
                    }

                    flushCommitBatch();
                    // Remaining keys stay queued for the next replay cycle.
                    performReplacement(diffPart);
                    oldPreBuffer_      = preeditStr;
                    replacementStarted = true;
                }
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
        const KeySym sym     = keyEvent.rawKey().sym();
        const bool   pressed = !keyEvent.isRelease();
        if (sym == FcitxKey_Shift_L) {
            shiftLeftHeld_ = pressed;
            LotusWlrVirtualKeyboard::instance().setUserShift(isShiftHeld());
        } else if (sym == FcitxKey_Shift_R) {
            shiftRightHeld_ = pressed;
            LotusWlrVirtualKeyboard::instance().setUserShift(isShiftHeld());
        }
    }

    void LotusState::forgetShiftState() {
        shiftLeftHeld_  = false;
        shiftRightHeld_ = false;
        LotusWlrVirtualKeyboard::instance().setUserShift(false);
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

} // namespace fcitx
