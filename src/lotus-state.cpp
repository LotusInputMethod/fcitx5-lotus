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
#include "lotus-utils.h"
#include "lotus-input-backend.hpp"
#include "lotus.h"
#include <cstddef>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/menu.h>
#include <fcitx/userinterface.h>
#include <algorithm>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
namespace fcitx {
    constexpr int      MAX_SCAN_LENGTH = 15;
    static inline bool isWordBreak(uint32_t ucs4) {
        if (__builtin_expect(ucs4 > 64, 1)) return false;
        if (__builtin_expect(ucs4 == 64, 0)) return true; // '@'
        // Bits set: NUL(0) TAB(9) LF(10) CR(13) SPC(32) :;<=>?(58-63)
        static constexpr uint64_t kMask =
            (1ULL << 0) | (1ULL << 9) | (1ULL << 10) | (1ULL << 13) | (1ULL << 32) | (1ULL << 58) | (1ULL << 59) | (1ULL << 60) | (1ULL << 61) | (1ULL << 62) | (1ULL << 63);
        bool r;
        asm("btq %1, %2\n\t"
            "setc %0"
            : "=r"(r)
            : "r"((uint64_t)ucs4), "r"(kMask)
            : "cc");
        return r;
    }
    inline void update_max(std::atomic<uint32_t>& value, uint32_t target) {
        uint32_t current = value.load(std::memory_order_acquire);
        asm volatile("1:\n\t"
                     "cmpl %[target], %[current]\n\t"
                     "jae 2f\n\t"
                     "lock cmpxchgl %[target], %[mem]\n\t"
                     "jne 1b\n\t"
                     "2:\n\t"
                     : [mem] "+m"(value), [current] "+a"(current)
                     : [target] "r"(target)
                     : "memory");
    }
    LotusState::LotusState(LotusEngine* engine, InputContext* ic) : engine_(engine), ic_(ic) { setEngine(); }
    void LotusState::setEngine() {
        inputBackend_.reset();
        inputBackend_ = makeLotusInputBackend();
        realMode      = modeStringToEnum(engine_->config().mode.value());
        inputBackend_->recreateEngine(engine_);
        setOption();
    }
    void LotusState::setOption() { if (!inputBackend_) return; inputBackend_->setOptions(engine_); }
    bool LotusState::connect_uinput_server() {
        if (uinput_client_fd_ >= 0) return true;
        const std::string current_path = buildSocketPath("kb_socket");
        int               current_fd   = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
        if (current_fd < 0) {
            LOTUS_ERROR("Failed to create socket: " + std::string(strerror(errno)));
            return false;
        }
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = '\0';
        memcpy(&addr.sun_path[1], current_path.c_str(), current_path.length());
        socklen_t len = offsetof(struct sockaddr_un, sun_path) + current_path.length() + 1;
        if (connect(current_fd, (struct sockaddr*)&addr, len) == 0) {
            uinput_client_fd_ = current_fd;
            return true;
        }
        LOTUS_ERROR("Failed to connect to socket: " + std::string(strerror(errno)));
        int old_fd = uinput_client_fd_.exchange(-1);
        if (old_fd != -1) close(old_fd);
        return false;
    }
    int LotusState::setup_uinput() { return connect_uinput_server() ? uinput_client_fd_.load(std::memory_order_acquire) : -1; }
    void LotusState::send_backspace_uinput(int count) const {
        if (uinput_client_fd_ < 0 && !connect_uinput_server()) {
            LOTUS_ERROR("Cannot send backspace since cannot connect to uinput server");
            return;
        }
        ssize_t n = send(uinput_client_fd_, &count, sizeof(count), MSG_NOSIGNAL);
        if (n < 0) {
            LOTUS_WARN("Failed to send backspace: " + std::string(strerror(errno)));
            int old_fd = uinput_client_fd_.exchange(-1);
            if (old_fd != -1) close(old_fd);
            if (connect_uinput_server()) {
                LOTUS_INFO("Reconnected to uinput server successfully");
                send(uinput_client_fd_, &count, sizeof(count), MSG_NOSIGNAL);
            }
        }
        //HACK
        if (waitAck_) {
            LOTUS_INFO("Waiting for ack");
            LOTUS_INFO("chrome x11 hit me");
            char ack;
            recv(uinput_client_fd_, &ack, sizeof(ack), MSG_NOSIGNAL);
            // keep safe that bs is finish by app
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // ez way but cause alot of problem
            //std::this_thread::sleep_for(std::chrono::milliseconds(count * 5));
        } else {
            LOTUS_INFO("firefox hit me");
            std::this_thread::sleep_for(std::chrono::milliseconds(count * 10));
        }
    }
    void LotusState::send_backspace_forward(int count) const {
        if (count <= 0) return;
        for (int i = 0; i < count; ++i) {
            ic_->forwardKey(Key(FcitxKey_BackSpace, KeyState::NoState), false);
            ic_->forwardKey(Key(FcitxKey_BackSpace, KeyState::NoState), true);
        }
    }
    void LotusState::finishReplacement() {
        is_deleting_.store(false, std::memory_order_release);
        replacement_start_ms_.store(0, std::memory_order_release);
        replacement_thread_id_.store(0, std::memory_order_release);
        expected_backspaces_     = 0;
        current_backspace_count_ = 0;
        pending_commit_string_.clear();
        buffered_keys_.clear();
    }
    bool LotusState::isAutofillCertain(const SurroundingText& s) {
        if (!s.isValid() || oldPreBuffer_.empty()) return false;
        const unsigned int cursor  = s.cursor();
        const unsigned int anchor  = s.anchor();
        const auto&        text    = s.text();
        const size_t       cursor_sz = static_cast<size_t>(cursor);
        // Fix that surrounding text is delay update
        const size_t buffLen    =
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
            utf8_length_avx512(oldPreBuffer_.data(), oldPreBuffer_.size());
#else
            utf8::length(oldPreBuffer_);
#endif
        const size_t pb         = text.find(oldPreBuffer_);
        size_t       rangeStart = buffLen >= cursor_sz ? 0 : cursor_sz - buffLen;
        const bool   sameprefix = pb != std::string::npos && pb >= rangeStart && pb <= cursor_sz;
        // Detect browser autofill/autocomplete suggestions via selection.
        // This check for wayland_input method v2/v3 and not dbus
        if (cursor != anchor) {
            LOTUS_INFO("check suggest wayland");
            unsigned int selectionStart = std::min(anchor, cursor);
            unsigned int selectionEnd   = std::max(anchor, cursor);
            // Only consider it browser autofill if the selection starts at the cursor
            // and extends to the end of the line (common address bar behavior).
            if (cursor <= selectionEnd) {
                if (!sameprefix) return false;
                // If the selection contains a newline, it's likely a multiline editor (AI ghost text),
                // not a single-line URL/Search bar.
                size_t p =
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
                    find_char_avx512(text.data(), text.size(), selectionStart, '\n');
#else
                    text.find('\n', selectionStart);
#endif
                return p == std::string::npos || p >= static_cast<size_t>(selectionEnd);
            }
        }
        const size_t textLen =
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
            utf8_length_avx512(text.data(), text.size());
#else
            utf8::length(text);
#endif
        if (textLen == cursor_sz) {
            realtextLen.store(textLen, std::memory_order_release);
            return false;
        }
        // Heuristic: rapid text growth in a single-line context.
        // Applied only when no newline is present after the cursor to distinguish from AI text in editors.
        // Check for wayland app that use dbus as backend
        if (textLen > cursor_sz)
            if(cursor == realtextLen.load(std::memory_order_acquire)
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
                && find_char_avx512(text.data(), text.size(), cursor, '\n') == static_cast<size_t>(-1)
#else
                && text.find('\n', cursor) == std::string::npos
#endif
                && sameprefix)
                return true;
        update_max(realtextLen, static_cast<uint32_t>(cursor));
        return false;
    }
    void LotusState::handlePreeditMode(KeyEvent& keyEvent, KeySym currentSym) {
        std::string commitStr;
        std::string preeditStr;
        bool        processed = inputBackend_->processKeyEventAndPull(currentSym, keyEvent.rawKey().states(), &commitStr, &preeditStr);
        if (processed) keyEvent.filterAndAccept();
        if (!commitStr.empty()) {
            LOTUS_INFO("Commit: " + commitStr);
            ic_->commitString(commitStr);
        }
        ic_->inputPanel().reset();
        if (!preeditStr.empty()) {
            std::string_view view = preeditStr;
            Text             text;
            TextFormatFlags  fmt = TextFormatFlag::NoFlag;
            if (utf8::validate(view)) text.append(std::string(view), fmt);
            text.setCursor(static_cast<int>(text.textLength()));
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) ic_->inputPanel().setClientPreedit(text);
            else ic_->inputPanel().setPreedit(text);
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    void LotusState::updateEmojiPageStatus(CommonCandidateList* commonList) {
        if ((commonList == nullptr) || commonList->empty()) return;
        int pageSize = commonList->pageSize();
        if (pageSize <= 0) pageSize = 9;
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
                    if (globalCursorIndex == totalSize - 1) {
                        commonList->setGlobalCursorIndex(globalCursorIndex);
                    } else if (localCursorIndex < pageSize - 1) {
                        commonList->setGlobalCursorIndex(globalCursorIndex + 1);
                    } else {
                        commonList->next();
                        int newPage = commonList->currentPage();
                        commonList->setGlobalCursorIndex(newPage * pageSize);
                    }
                    handled = true;
                    break;
                }
                case FcitxKey_ISO_Left_Tab:
                case FcitxKey_Up: {
                    if (globalCursorIndex == 0) {
                        commonList->setGlobalCursorIndex(globalCursorIndex);
                    } else if (localCursorIndex > 0) {
                        commonList->setGlobalCursorIndex(globalCursorIndex - 1);
                    } else {
                        commonList->prev();
                        int newPage  = commonList->currentPage();
                        int newIndex = (newPage * pageSize) + pageSize - 1;
                        commonList->setGlobalCursorIndex(newIndex);
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
                if (isCtrlBackspace) { emojiBuffer_.clear();
                } else {
                    emojiBuffer_.pop_back();
                    while (!emojiBuffer_.empty() && (emojiBuffer_.back() & 0xC0) == 0x80)
                        emojiBuffer_.pop_back();
                }
                keyEvent.filterAndAccept();
            } else keyEvent.forward();
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
                } else keyEvent.forward();
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
        std::string utf8Char = Key::keySymToUTF8(currentSym);
        if (!utf8Char.empty()) {
            emojiBuffer_.append(utf8Char);
            keyEvent.filterAndAccept();
            updateEmojiPreedit();
        } else keyEvent.forward();
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
        } else emojiCandidates_ = engine_->emojiLoader().search(emojiBuffer_);
        if (!emojiBuffer_.empty()) {
            Text preeditText;
            preeditText.append(emojiBuffer_, TextFormatFlag::Underline);
            preeditText.setCursor(static_cast<int>(preeditText.textLength()));
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) ic_->inputPanel().setClientPreedit(preeditText);
            else ic_->inputPanel().setPreedit(preeditText);
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
                if (emojiBuffer_.empty()){displayLabel.append(std::to_string(localIndex) + ": " + emojiCandidates_[i].output, TextFormatFlag::NoFlag);
                }else{displayLabel.append(std::to_string(localIndex)+": "+ emojiCandidates_[i].trigger + " " + emojiCandidates_[i].output,TextFormatFlag::NoFlag);}
                candidateList->append(std::make_unique<EmojiCandidateWord>(displayLabel, this, emojiCandidates_[i]));
            }
            candidateList->setGlobalCursorIndex(0);
            ic_->inputPanel().setCandidateList(std::move(candidateList));
            auto currentList = std::dynamic_pointer_cast<CommonCandidateList>(ic_->inputPanel().candidateList());
            updateEmojiPageStatus(currentList.get());
        } else ic_->inputPanel().setCandidateList(nullptr);
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    bool LotusState::handleUInputKeyPress(KeyEvent& event, KeySym currentSym, int sleepTime) {
        if (!is_deleting_.load()) return false;
        if (isBackspace(currentSym)) {
            current_backspace_count_ += 1;
            if (current_backspace_count_ < expected_backspaces_) return false; // Allow intermediate backspaces to reach the app to clear autofill/old text.
            int64_t elapsed_ms = now_ms() - replacement_start_ms_.load(std::memory_order_acquire);
            int64_t wait_ms    = static_cast<int64_t>(sleepTime) - elapsed_ms;
            if (wait_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            if (waitAck_) std::this_thread::sleep_for(std::chrono::milliseconds(5)); //wait more
            ic_->commitString(pending_commit_string_);
            LOTUS_INFO("Commit: " + pending_commit_string_);
            finishReplacement();
            event.filterAndAccept(); // Filter out the final trigger backspace.
            return true;
        }
        return false;
    }
    bool LotusState::performReplacement(const std::string& deletedPart, const std::string& addedPart) {
        LOTUS_INFO("Perform replacement: " + deletedPart + " -> " + addedPart); //NOLINT
        int my_id                  = ++current_thread_id_;
        current_backspace_count_   = 0;
        pending_commit_string_     = addedPart;
        const auto& surrounding    = ic_->surroundingText();
        int         autofillOffset = isAutofillCertain(surrounding) ? 1 : 0;
        expected_backspaces_       = static_cast<int>(
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
            utf8_length_avx512(deletedPart.data(), deletedPart.size())
#else
            utf8::length(deletedPart)
#endif
        ) + 1 + autofillOffset;
        if (realMode == LotusMode::UinputWine) expected_backspaces_-=1;
        // Use deleteSurroundingText for apps that support it for smooth typing
        LOTUS_INFO("surr: \""+surrounding.text()+"\"");
        if (surrtp) LOTUS_INFO("surrtp");
        if ((false || surrtp) // Lmfao, only this work :>
            && (surrounding.isValid() && ic_->capabilityFlags().test(CapabilityFlag::SurroundingText))
              && (!surrounding.text().empty() && surrounding.text().back() != '\n' // firefox and discord insert '\n' into surr cause bug
                && !autofillOffset)                                                // TODO: Guard, remove this when bug of surrounding is fixes
        ) {
            LOTUS_INFO("deleteSurroundingText branch");
            const int bsCount = static_cast<int>(
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
                  utf8_length_avx512(deletedPart.data(), deletedPart.size())
#else
                  utf8::length(deletedPart)
#endif
            );
            if (autofillOffset) {
                LOTUS_INFO("have suggestions branch");
                const auto surr = surrounding.text();
                int surrLen       = static_cast<int>(
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
                  utf8_length_avx512(surr.data(), surr.size())
#else
                  utf8::length(surr)
#endif
                );
                auto cur          = static_cast<size_t>(surrounding.cursor());
                int realLen       = static_cast<int>(cur);
                int suggestionLen = surrLen - realLen;
                // delete suggestion tail
                if (suggestionLen > 0) ic_->deleteSurroundingText(0, 1);
            }
            if (bsCount > 0) ic_->deleteSurroundingText(-bsCount, static_cast<unsigned int>(bsCount));
            ic_->commitString(addedPart);
            return true;
        } else {
            replacement_thread_id_.store(my_id, std::memory_order_release);
            replacement_start_ms_.store(now_ms(), std::memory_order_release);
            is_deleting_.store(true, std::memory_order_release);
            monitor_cv.notify_one();
            if (0 && isTerm) {
                send_backspace_forward(expected_backspaces_ - 1);
                return true;
            } else send_backspace_uinput(expected_backspaces_);
            LOTUS_INFO("Send " + std::to_string(expected_backspaces_ - 1 - autofillOffset) + " backspaces + 1 trigger");
            if (autofillOffset) LOTUS_INFO("Send more 1 extra delete suggestions");
        }
        return false;
    }
    bool LotusState::checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym) {
        if (keyEvent.key().isCursorMove() || currentSym == FcitxKey_Tab || currentSym == FcitxKey_KP_Tab || currentSym == FcitxKey_ISO_Left_Tab || currentSym == FcitxKey_Escape ||
            keyEvent.key().hasModifier()) {
            finishReplacement();
            hasHistory_ = false;
            inputBackend_->resetEngine();
            oldPreBuffer_.clear();
            return true;
        }
        if (currentSym == FcitxKey_Delete) return true;
        if (currentSym >= FcitxKey_KP_0 && currentSym <= FcitxKey_KP_9) {
            currentSym = static_cast<KeySym>(FcitxKey_0 + (currentSym - FcitxKey_KP_0));
            return false;
        }
        switch (currentSym) {
            case FcitxKey_KP_Add:      currentSym = FcitxKey_plus; break;
            case FcitxKey_KP_Subtract: currentSym = FcitxKey_minus; break;
            case FcitxKey_KP_Divide:   currentSym = FcitxKey_slash; break;
            case FcitxKey_KP_Multiply: currentSym = FcitxKey_asterisk; break;
            case FcitxKey_KP_Decimal:  currentSym = FcitxKey_period; break;
            case FcitxKey_KP_Enter:    currentSym = FcitxKey_Return; break;
            case FcitxKey_KP_Equal:    currentSym = FcitxKey_equal; break;
            case FcitxKey_KP_Space:    currentSym = FcitxKey_space; break;
            default: break;
        }
        return false;
    }
    void LotusState::handleUinputMode(KeyEvent& keyEvent, KeySym currentSym) {
        if (checkForwardSpecialKey(keyEvent, currentSym)) {
            keyEvent.forward();
            return;
        }
        if (uinput_client_fd_ < 0) setup_uinput();
        if (isBackspace(currentSym) || currentSym == FcitxKey_Return) {
            if (isBackspace(currentSym)) {
                hasHistory_ = true;
                std::string preBs;
                inputBackend_->processKeyEventAndPull(FcitxKey_BackSpace, 0, nullptr, &preBs);
                oldPreBuffer_ = preBs;
            } else {
                hasHistory_ = false;
                inputBackend_->resetEngine();
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
        std::string commitStr;
        std::string preeditStrBuf;
        bool        processed = inputBackend_->processKeyEventAndPull(currentSym, keyEvent.rawKey().states(), &commitStr, &preeditStrBuf);
        if (!commitStr.empty()) {
            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldPreBuffer_, commitStr, deletedPart, addedPart);
            if (!deletedPart.empty()) {
                keyEvent.filterAndAccept();
                performReplacement(deletedPart, addedPart);
            } else {
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!addedPart.empty() && (keyUtf8 != addedPart || wasAutoCapitalized)) {
                    // Prevent auto-capitalized character replacement from stripping out Vietnamese chars
                    if (addedPart.size() > 1 && addedPart.back() == ' ')
                        // Stripping the trigger key (space) from addedPart
#if __cplusplus >= 202002L
                        addedPart.resize(addedPart.size() - 1);
#else
                        addedPart = addedPart.substr(0, addedPart.size() - 1);
#endif
                    ic_->commitString(addedPart);
                    LOTUS_INFO("Commit: " + addedPart);
                    keyEvent.filterAndAccept();
                } else keyEvent.forward();
            }
            hasHistory_ = false;
            inputBackend_->resetEngine();
            oldPreBuffer_.clear();
            return;
        }
        // Treat "processed but no effect" as passthrough
        if (!processed || (!commitStr.empty() && !preeditStrBuf.empty())) {
            if (!preeditStrBuf.empty()) {
                hasHistory_ = false;
                inputBackend_->resetEngine();
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
            return;
        }
        hasHistory_ = true;
        realtextLen.fetch_add(1, std::memory_order_acq_rel);
        std::string preeditStr = preeditStrBuf;
        std::string deletedPart;
        std::string addedPart;
        wa_flag = false;
        if (wa_flag) keyEvent.filterAndAccept();
        if (compareAndSplitStrings(oldPreBuffer_, preeditStr, deletedPart, addedPart) != 0) {
            if (deletedPart.empty()) {
                bool isCommit           = false;
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!addedPart.empty()) {
                    if (wa_flag) ic_->commitString(addedPart);
                    oldPreBuffer_ = preeditStr;
                    if (!wa_flag)
                        if (wasAutoCapitalized || addedPart != keyUtf8) {
                            LOTUS_INFO("Commit: " + addedPart);
                            ic_->commitString(addedPart);
                            keyEvent.filterAndAccept();
                            isCommit = true;
                        }
                }
                if (!wa_flag && !isCommit) {
                    keyEvent.forward();
                }
            } else {
                if (uinput_client_fd_ < 0) {
                    LOTUS_ERROR("Cannot connect to uinput server, commit rawkey");
                    std::string rawKey = keyEvent.key().toString();
                    if (!rawKey.empty()) ic_->commitString(rawKey);
                    return;
                }
                if (is_deleting_.load()) finishReplacement();
                if (!wa_flag) keyEvent.filterAndAccept();
                performReplacement(deletedPart, addedPart);
                oldPreBuffer_ = preeditStr;
            }
        }
    }
    void LotusState::handleSurroundingText(KeyEvent& keyEvent, KeySym currentSym) {
        if (checkForwardSpecialKey(keyEvent, currentSym)) {
            keyEvent.forward();
            return;
        }
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
            inputBackend_->resetEngine();
            keyEvent.forward();
            return;
        }
        if (surrounding.anchor() != surrounding.cursor()) ic->deleteSurroundingText(0, 0);
        const std::string& text   = surrounding.text();
        unsigned int       cursor = surrounding.cursor();
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
                    while (prev != text.begin() && ((*prev & 0xC0) == 0x80)) { --prev;}
                }
                uint32_t ucs4 = utf8::getChar(prev, text.end());
                if (isWordBreak(ucs4)) break;
                startIter = prev;
                ++scanCount;
            }
            std::string oldWord(startIter, endIter);
            if (oldWord.empty()) { processNormalKey(keyEvent, currentSym);return;}
            inputBackend_->rebuildFromText(oldWord.c_str());
            bool processed = inputBackend_->processKeyEvent(currentSym, keyEvent.rawKey().states());
            if (!processed) {
                keyEvent.forward();
                inputBackend_->resetEngine();
                return;
            }
            std::string commitPart;
            std::string preeditPart;
            inputBackend_->pullCommitAndPreedit(&commitPart, &preeditPart);
            std::string newWord;
            if (!commitPart.empty()) newWord += commitPart;
            if (!preeditPart.empty()) newWord += preeditPart;
            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldWord, newWord, deletedPart, addedPart);
            if (deletedPart.empty() && addedPart == keyEvent.key().toString()) {
                inputBackend_->resetEngine();
                keyEvent.forward();
                return;
            }
            if (!deletedPart.empty() || !addedPart.empty()) {
                size_t charsToDelete = utf8::length(deletedPart);
                if (charsToDelete > 0) ic->deleteSurroundingText(-static_cast<int>(charsToDelete), static_cast<int>(charsToDelete));
                if (!addedPart.empty()) {ic->commitString(addedPart);LOTUS_INFO("Commit: " + addedPart);}
            }
            inputBackend_->resetEngine();
            keyEvent.filterAndAccept();
            return;
        }
    }
    void LotusState::processNormalKey(KeyEvent& keyEvent, KeySym currentSym) {
        auto* ic = keyEvent.inputContext();
        inputBackend_->resetEngine();
        std::string commitPart;
        std::string preeditPart;
        bool        processed = inputBackend_->processKeyEventAndPull(currentSym, keyEvent.rawKey().states(), &commitPart, &preeditPart);
        if (processed) {
            std::string out;
            if (!commitPart.empty()) out += commitPart;
            if (!preeditPart.empty()) out += preeditPart;
            if (!out.empty()) {LOTUS_INFO("Commit: " + out);ic->commitString(out);}
            inputBackend_->resetEngine();
            keyEvent.filterAndAccept();
        } else keyEvent.forward();
    }
    void LotusState::handleDoubleSpaceReplacement() {
        switch (realMode) {
            case LotusMode::SurroundingText: {
                ic_->deleteSurroundingText(-1, 1);
                ic_->commitString(". ");
                LOTUS_INFO("Commit: . ");
                break;
            }
            default: { // Uinput, Smooth, Preedit, etc.
                performReplacement(" ", ". ");
                LOTUS_INFO("Commit: . ");
                break;
            }
        }
        if (*engine_->config().autoCapitalizeAfterPunctuation) {
            isPrevPunctuation_ = true;
            shouldCapitalize_  = true;
        }
    }
    void LotusState::keyEvent(KeyEvent& keyEvent) {
        if (!inputBackend_ || keyEvent.isRelease()) return;
        if (uinput_client_fd_ < 0) {
            LOTUS_WARN("uinput connect failed, reconnecting....");
            connect_uinput_server();
        }
        if (current_backspace_count_ >= expected_backspaces_ && is_deleting_.load()) {
            finishReplacement();
        }
        if (needEngineReset.load() && realMode != LotusMode::Off) {
            LOTUS_INFO("Need engine reset");
            oldPreBuffer_.clear();
            hasHistory_ = false;
            inputBackend_->resetEngine();
            finishReplacement();
            isPrevSpace_             = false;
            shouldCapitalize_        = false;
            isPrevPunctuation_       = false;
            needEngineReset.store(false);
        }
        if (g_mouse_clicked.load(std::memory_order_acquire) && !is_deleting_.load(std::memory_order_acquire)) {
            g_mouse_clicked.store(false, std::memory_order_release);
            clearAllBuffers();
        }
        KeySym currentSym = keyEvent.rawKey().sym();
        if (needFallbackCommit.load(std::memory_order_acquire)) {
            LOTUS_INFO("Need fallback commit");
            needFallbackCommit.store(false, std::memory_order_release);
            if (current_thread_id_.load(std::memory_order_acquire) == replacement_thread_id_.load(std::memory_order_acquire))
                if (!pending_commit_string_.empty()) {
                    ic_->commitString(pending_commit_string_);
                }
            finishReplacement();
            if (isBackspace(currentSym)) {
                keyEvent.filterAndAccept();
                return;
            }
        }
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
                    } else if (currentSym != FcitxKey_space) shouldCapitalize_ = false;
                }
                switch (currentSym) {
                    case FcitxKey_period:
                    case FcitxKey_exclam:
                    case FcitxKey_question: isPrevPunctuation_ = true; break;
                    case FcitxKey_Return:
                    case FcitxKey_KP_Enter: shouldCapitalize_  = true; isPrevPunctuation_ = false; break;
                    case FcitxKey_space: if (isPrevPunctuation_) { shouldCapitalize_  = true; isPrevPunctuation_ = false;} break;
                    default: if (currentSym != FcitxKey_space) isPrevPunctuation_ = false; break;
                }
            }
        }
        if (is_deleting_.load(std::memory_order_acquire)) {
            if (isBackspace(currentSym)) {
                if (realtextLen.load(std::memory_order_acquire) > 0) realtextLen.fetch_sub(1, std::memory_order_acq_rel);
                if (handleUInputKeyPress(keyEvent, currentSym, (realMode == LotusMode::Smooth) ? 3 : 10)) return;
            } else {
                std::string keyUtf8Check = Key::keySymToUTF8(currentSym);
                if (!keyUtf8Check.empty() && buffered_keys_.size() < MAX_BUFFERED_KEYS) {
                    LOTUS_WARN("Typing so fast, add key to queue");
                    buffered_keys_.push_back({.sym = currentSym, .state = keyEvent.rawKey().states()});
                }
                keyEvent.filterAndAccept();
            }
            return;
        }
        if (*engine_->config().doubleSpaceToPeriod && realMode != LotusMode::Off) {
            if (currentSym == FcitxKey_space) {
                if (isPrevSpace_) {
                    keyEvent.filterAndAccept();
                    handleDoubleSpaceReplacement();
                    isPrevSpace_ = false;
                    return;
                }
                isPrevSpace_ = true;
            } else isPrevSpace_ = false;
        }
        switch (realMode) {
            case LotusMode::Uinput: case LotusMode::Smooth: case LotusMode::UinputWine: handleUinputMode(keyEvent, currentSym); break;
            case LotusMode::SurroundingText: handleSurroundingText(keyEvent, currentSym); break;
            case LotusMode::Preedit:         handlePreeditMode(keyEvent, currentSym); break;
            case LotusMode::Emoji:           handleEmojiMode(keyEvent); break;
            default: break;
        }
    }
    void LotusState::reset(bool isFocusOut) {
        const auto& surrounding = ic_->surroundingText();
        const auto& text        = surrounding.text();
        const size_t textLen =
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
            utf8_length_avx512(text.data(), text.size());
#else
            utf8::length(text);
#endif
        realtextLen.store(textLen, std::memory_order_release);
        if (is_deleting_.load(std::memory_order_acquire)) return;
        if (inputBackend_) {
            isPrevSpace_       = false;
            shouldCapitalize_  = false;
            isPrevPunctuation_ = false;
            if (realMode == LotusMode::Preedit && isFocusOut) {
                inputBackend_->commitPreedit();
                std::string commit;
                inputBackend_->pullCommit(&commit);
                if (!commit.empty()) { ic_->commitString(commit); LOTUS_INFO("Commit: "+commit);}
            }
            inputBackend_->resetEngine();
        }
        if (getFrontendName(ic_) != "dbus") clearAllBuffers();
        if (realMode == LotusMode::Off) return;
        ic_->inputPanel().reset();
        switch (realMode) {
            case LotusMode::Preedit:
            case LotusMode::Emoji:
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            default: break;
        }
    }
    void LotusState::commitBuffer() {
        if (realMode == LotusMode::Off) return;
        if (inputBackend_) inputBackend_->resetEngine();
        switch (realMode) {
            case LotusMode::Preedit:
                ic_->inputPanel().reset();
                if (inputBackend_) {
                    inputBackend_->commitPreedit();
                    std::string commit;
                    inputBackend_->pullCommit(&commit);
                    if (!commit.empty()) ic_->commitString(commit);
                }
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            default: break;
        }
    }
    void LotusState::clearAllBuffers() {
        LOTUS_DEBUG("Clear all buffers");
        if (is_deleting_.load(std::memory_order_acquire)) return;
        oldPreBuffer_.clear();
        hasHistory_              = false;
        expected_backspaces_     = 0;
        current_backspace_count_ = 0;
        pending_commit_string_.clear();
        emojiBuffer_.clear();
        emojiCandidates_.clear();
        buffered_keys_.clear();
        shouldCapitalize_  = false;
        isPrevPunctuation_ = false;
        if (inputBackend_) inputBackend_->resetEngine();
    }
    bool LotusState::isEmptyHistory() const { return !hasHistory_;}
    bool LotusState::isReplacing() const { return expected_backspaces_ > 0 && current_backspace_count_ < expected_backspaces_;}
    bool LotusState::isX11() const { return false; /*cat /proc/<PID>/maps | grep -E 'libX11|libxcb' */}
} // namespace fcitx
