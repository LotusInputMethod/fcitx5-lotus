/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Thin wrapper around fcitx5-unikey's UkEngine stack
 * LOTUS_USE_UNIKEY is wired through LotusState.
 */
#ifndef FCITX5_LOTUS_LOTUS_UNIKEY_ENGINE_HPP
#define FCITX5_LOTUS_LOTUS_UNIKEY_ENGINE_HPP

#include "keycons.h"
#include "vnlexi.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class UnikeyInputMethod;
class UnikeyInputContext;

namespace fcitx::lotus {

    class LotusUnikeyEngine {
      public:
        LotusUnikeyEngine();
        ~LotusUnikeyEngine();

        LotusUnikeyEngine(const LotusUnikeyEngine&)            = delete;
        LotusUnikeyEngine& operator=(const LotusUnikeyEngine&) = delete;
        LotusUnikeyEngine(LotusUnikeyEngine&&)                 = delete;
        LotusUnikeyEngine&   operator=(LotusUnikeyEngine&&)    = delete;

        void                 setInputMethod(UkInputMethod im);
        void                 setOutputCharset(int charsetId);
        void                 setOptions(UnikeyOptions* opt);

        void                 resetBuf();
        void                 setCapsState(int shiftPressed, int capsLockOn);
        void                 filter(std::uint32_t unikeyKeyCode);
        void                 putChar(std::uint32_t ch);
        void                 rebuildChar(VnLexiName ch);
        void                 backspacePress();
        void                 restoreKeyStrokes();

        bool                 isAtWordBeginning() const;

        int                  backspaces() const;
        int                  bufChars() const;
        const unsigned char* buf() const;

        UnikeyInputMethod*   inputMethod();
        UnikeyInputContext*  context();

      private:
        std::unique_ptr<UnikeyInputMethod>  im_;
        std::unique_ptr<UnikeyInputContext> uic_;
    };

} // namespace fcitx::lotus

#endif
