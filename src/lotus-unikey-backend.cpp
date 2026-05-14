/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Native Unikey engine (fcitx5-unikey patterns; no Go/CGO).
 */
#ifdef LOTUS_ENGINE_UNIKEY
#include "lotus-input-backend.hpp"
#include "lotus-config.h"
#include "lotus-engine.h"
#include "../unikey/LotusUnikeyEngine.hpp"
#include "unikeyinputcontext.h"
#include <vnconv.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <unordered_set>
namespace fcitx {
    namespace {
        static bool isWordBreakSym(unsigned char c) {
            static const std::unordered_set<unsigned char> WordBreakSyms = {
                ',', ';', ':', '.', '\"', '\'', '!', '?', ' ',
            };
            return WordBreakSyms.contains(c);
        }
        static UkInputMethod mapLotusIm(const std::string& name) {
            if (name.find("Telex 2") != std::string::npos && name.find("VNI") == std::string::npos)
                return UkSimpleTelex2;
            if (name.find("VNI") != std::string::npos || name == "VNI")
                return UkVni;
            if (name.find("VIQR") != std::string::npos)
                return UkViqr;
            if (name.find("Microsoft") != std::string::npos || name.find("Ms") != std::string::npos)
                return UkMsVi;
            if (name.find("Telex") != std::string::npos)
                return UkSimpleTelex;
            if (name.find("Telex + VNI") != std::string::npos)
                return UkTelex;
            return UkSimpleTelex;
        }
        class LotusUnikeyInputBackend final : public LotusInputBackend {
          public:
            void recreateEngine(LotusEngine* engine) override {
                engineRef_ = engine;
                uk_        = std::make_unique<::fcitx::lotus::LotusUnikeyEngine>();
                applyFromConfig(engine);
                resetEngine();
            }
            void setOptions(LotusEngine* engine) override {
                applyFromConfig(engine);
            }
            void resetEngine() override {
                pendingPullCommit_.clear();
                preeditStr_.clear();
                lastShiftPressed_ = FcitxKey_None;
                lastKeyWithShift_ = false;
                autoCommit_       = false;
                if (uk_) uk_->resetBuf();
            }
            void rebuildFromText(const char* utf8) override {
                resetEngine();
                if (!uk_ || utf8 == nullptr) return;
                for (auto ucs : utf8::MakeUTF8CharRange(std::string_view(utf8))) {
                    if (ucs < 128U)
                        uk_->putChar(static_cast<unsigned int>(ucs));
                    else uk_->putChar(ucs);
                }
                syncState(FcitxKey_None);
            }
            bool processKeyEventAndPull(uint32_t sym, uint32_t state, std::string* commit, std::string* preedit) override {
                pendingPullCommit_.clear();
                bool ok = dispatch(sym, state);
                if (commit) *commit = pendingPullCommit_;
                if (preedit) *preedit = preeditStr_;
                pendingPullCommit_.clear();
                return ok;
            }
            bool processKeyEvent(uint32_t sym, uint32_t state) override {
                pendingPullCommit_.clear();
                return dispatch(sym, state);
            }
            void pullCommitAndPreedit(std::string* commit, std::string* preedit) override {
                if (commit) *commit = pendingPullCommit_;
                if (preedit) *preedit = preeditStr_;
                pendingPullCommit_.clear();
            }
            void pullCommit(std::string* out) override {
                if (out) *out = pendingPullCommit_;
                pendingPullCommit_.clear();
            }
            void pullPreedit(std::string* out) override {
                if (out) *out = preeditStr_;
            }
            void commitPreedit() override {
                if (!preeditStr_.empty()) pendingPullCommit_ = preeditStr_;
                preeditStr_.clear();
                if (uk_) uk_->resetBuf();
            }
          private:
            void applyFromConfig(LotusEngine* engine) {
                if (!uk_) return;
                UkInputMethod currentIM_ = mapLotusIm(engine->config().inputMethod.value());
                uk_->setInputMethod(currentIM_);
                uk_->setOutputCharset(CONV_CHARSET_XUTF8);
                UnikeyOptions opt{};
                opt.freeMarking         = *engine->config().freeMarking ? 1 : 0;
                opt.modernStyle         = *engine->config().modernStyle ? 1 : 0;
                opt.macroEnabled        = *engine->config().enableMacro ? 1 : 0;
                opt.useUnicodeClipboard = 0;
                opt.alwaysMacro         = 0;
                opt.strictSpellCheck    = 0;
                opt.useIME              = 0;
                opt.spellCheckEnabled   = *engine->config().spellCheck ? 1 : 0;
                opt.autoNonVnRestore    = *engine->config().autoNonVnRestore ? 1 : 0;
                uk_->setOptions(&opt);
            }
            void eraseChars(int num_chars) {
                int           i;
                int           k = num_chars;
                unsigned char c = 0;
                for (i = static_cast<int>(preeditStr_.length()) - 1; i >= 0 && k > 0; --i) {
                    c = preeditStr_.at(static_cast<size_t>(i));
                    if (c < (unsigned char)'\x80' || c >= (unsigned char)'\xC0')
                        --k;
                }
                preeditStr_.erase(static_cast<size_t>(i + 1));
            }
            void syncState(KeySym sym) {
                auto* uic = uk_->context();
                if (uic->backspaces() > 0) {
                    if (static_cast<int>(preeditStr_.length()) <= uic->backspaces())
                        preeditStr_.clear();
                    else
                        eraseChars(uic->backspaces());
                }
                if (uic->bufChars() > 0) {
                    preeditStr_.append(reinterpret_cast<const char*>(uic->buf()), static_cast<size_t>(uic->bufChars()));
                } else if (sym != FcitxKey_Shift_L && sym != FcitxKey_Shift_R && sym != FcitxKey_None) {
                    preeditStr_.append(utf8::UCS4ToUTF8(sym));
                }
            }
            bool dispatch(uint32_t sym, uint32_t state) {
                if (!uk_) return false;
                KeyStates  st(static_cast<KeyStates>(state));
                const auto rawSym = static_cast<KeySym>(sym);
                if (st.testAny(KeyState::Ctrl_Alt) || rawSym == FcitxKey_Control_L || rawSym == FcitxKey_Control_R || rawSym == FcitxKey_Tab || rawSym == FcitxKey_Return ||
                    rawSym == FcitxKey_Delete || rawSym == FcitxKey_KP_Enter || (rawSym >= FcitxKey_Home && rawSym <= FcitxKey_Insert) ||
                    (rawSym >= FcitxKey_KP_Home && rawSym <= FcitxKey_KP_Delete)) {
                    uk_->context()->filter(0);
                    if (!preeditStr_.empty()) pendingPullCommit_ = preeditStr_;
                    preeditStr_.clear();
                    uk_->resetBuf();
                    return false;
                }
                if (st.test(KeyState::Super)) return false;
                if ((rawSym >= FcitxKey_Caps_Lock && rawSym <= FcitxKey_Hyper_R) || rawSym == FcitxKey_Shift_L || rawSym == FcitxKey_Shift_R) return false;
                if (rawSym == FcitxKey_BackSpace) {
                    uk_->backspacePress();
                    if (uk_->context()->backspaces() == 0 || preeditStr_.empty()) {
                        if (!preeditStr_.empty()) pendingPullCommit_ = preeditStr_;
                        preeditStr_.clear();
                        uk_->resetBuf();
                        return !pendingPullCommit_.empty();
                    }
                    if (static_cast<int>(preeditStr_.length()) <= uk_->context()->backspaces())
                        preeditStr_.clear();
                    else
                        eraseChars(uk_->context()->backspaces());
                    if (uk_->context()->bufChars() > 0)
                        preeditStr_.append(reinterpret_cast<const char*>(uk_->context()->buf()), static_cast<size_t>(uk_->context()->bufChars()));
                    return true;
                }
                if (rawSym >= FcitxKey_KP_Multiply && rawSym <= FcitxKey_KP_9) {
                    uk_->context()->filter(0);
                    if (!preeditStr_.empty()) pendingPullCommit_ = preeditStr_;
                    preeditStr_.clear();
                    uk_->resetBuf();
                    return false;
                }
                if (rawSym >= FcitxKey_space && rawSym <= FcitxKey_asciitilde) {
                    //const bool beginWord = uk_->isAtWordBeginning();
                    uk_->setCapsState(st.test(KeyState::Shift) ? 1 : 0, st.test(KeyState::CapsLock) ? 1 : 0);
                    uk_->filter(sym);
                    syncState(rawSym);
                    if (!preeditStr_.empty() && preeditStr_.back() == static_cast<char>(sym) && isWordBreakSym(static_cast<unsigned char>(sym))) {
                        pendingPullCommit_ = preeditStr_;
                        preeditStr_.clear();
                        uk_->resetBuf();
                        return true;
                    }
                    return true;
                }
                uk_->context()->filter(0);
                syncState(rawSym);
                if (!preeditStr_.empty()) pendingPullCommit_ = preeditStr_;
                preeditStr_.clear();
                uk_->resetBuf();
                return false;
            }
            std::unique_ptr<::fcitx::lotus::LotusUnikeyEngine> uk_;
            LotusEngine* engineRef_ = nullptr;
            std::string preeditStr_;
            std::string pendingPullCommit_;
            KeySym lastShiftPressed_ = FcitxKey_None;
            bool lastKeyWithShift_ = false;
            bool autoCommit_       = false;
        };
    } // namespace
    std::unique_ptr<LotusInputBackend> makeLotusInputBackend() {
        return std::make_unique<LotusUnikeyInputBackend>();
    }
} // namespace fcitx
#endif // LOTUS_ENGINE_UNIKEY
