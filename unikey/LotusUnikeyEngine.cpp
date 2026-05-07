/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "LotusUnikeyEngine.hpp"
#include "unikeyinputcontext.h"

namespace fcitx::lotus {

    LotusUnikeyEngine::LotusUnikeyEngine() : im_(std::make_unique<UnikeyInputMethod>()), uic_(std::make_unique<UnikeyInputContext>(im_.get())) {}

    LotusUnikeyEngine::~LotusUnikeyEngine() = default;

    void LotusUnikeyEngine::setInputMethod(UkInputMethod im) {
        im_->setInputMethod(im);
    }

    void LotusUnikeyEngine::setOutputCharset(int charsetId) {
        im_->setOutputCharset(charsetId);
    }

    void LotusUnikeyEngine::setOptions(UnikeyOptions* opt) {
        im_->setOptions(opt);
    }

    void LotusUnikeyEngine::resetBuf() {
        uic_->resetBuf();
    }

    void LotusUnikeyEngine::setCapsState(int shiftPressed, int capsLockOn) {
        uic_->setCapsState(shiftPressed, capsLockOn);
    }

    void LotusUnikeyEngine::filter(std::uint32_t unikeyKeyCode) {
        uic_->filter(unikeyKeyCode);
    }

    void LotusUnikeyEngine::putChar(std::uint32_t ch) {
        uic_->putChar(ch);
    }

    void LotusUnikeyEngine::rebuildChar(VnLexiName ch) {
        uic_->rebuildChar(ch);
    }

    void LotusUnikeyEngine::backspacePress() {
        uic_->backspacePress();
    }

    void LotusUnikeyEngine::restoreKeyStrokes() {
        uic_->restoreKeyStrokes();
    }

    bool LotusUnikeyEngine::isAtWordBeginning() const {
        return uic_->isAtWordBeginning();
    }

    int LotusUnikeyEngine::backspaces() const {
        return uic_->backspaces();
    }

    int LotusUnikeyEngine::bufChars() const {
        return uic_->bufChars();
    }

    const unsigned char* LotusUnikeyEngine::buf() const {
        return uic_->buf();
    }

    UnikeyInputMethod* LotusUnikeyEngine::inputMethod() {
        return im_.get();
    }

    UnikeyInputContext* LotusUnikeyEngine::context() {
        return uic_.get();
    }

} // namespace fcitx::lotus
