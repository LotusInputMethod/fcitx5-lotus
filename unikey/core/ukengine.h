/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __UKENGINE_H
#define __UKENGINE_H

#include "charset.h"
#include "inputproc.h"
#include "mactab.h"
#include "vnlexi.h"
#include <functional>

// This is a shared object among processes, do not put any pointer in it
struct UkSharedMem {
    // states
    bool vietKey;

    UnikeyOptions options;
    UkInputProcessor input;
    bool usrKeyMapLoaded;
    int usrKeyMap[256];
    int charsetId;

    CMacroTable macStore;
};

#define MAX_UK_ENGINE 128

enum VnWordForm { vnw_nonVn, vnw_empty, vnw_c, vnw_v, vnw_cv, vnw_vc, vnw_cvc };

typedef std::function<void(int *pShiftPressed, int *pCapslockOn)>
    CheckKeyboardCaseCb;

struct KeyBufEntry {
    UkKeyEvent ev;
    bool converted;
};

class UkEngine {
public:
    UkEngine();
    void setCtrlInfo(UkSharedMem *p) { m_pCtrl = p; }

    void setCheckKbCaseFunc(CheckKeyboardCaseCb pFunc) {
        m_keyCheckFunc = pFunc;
    }

    bool atWordBeginning() const;

    int process(unsigned int keyCode, int &backs, unsigned char *outBuf,
                int &outSize, UkOutputType &outType);
    // just pass through without filtering
    void pass(int keyCode);
    // rebuild preedit from surrounding char
    void rebuildChar(VnLexiName ch, int &backs, unsigned char *outBuf,
                     int &outSize);

    void setSingleMode();

    int processBackspace(int &backs, unsigned char *outBuf, int &outSize,
                         UkOutputType &outType);
    void reset();
    int restoreKeyStrokes(int &backs, unsigned char *outBuf, int &outSize,
                          UkOutputType &outType);

    // following methods must be public just to enable the use of pointers to
    // them they should not be called from outside.
    int processTone(UkKeyEvent &ev);
    int processRoof(UkKeyEvent &ev);
    int processHook(UkKeyEvent &ev);
    int processAppend(UkKeyEvent &ev);
    int appendVowel(UkKeyEvent &ev);
    int appendConsonnant(UkKeyEvent &ev);
    int processDd(UkKeyEvent &ev);
    int processMapChar(UkKeyEvent &ev);
    int processTelexW(UkKeyEvent &ev);
    int processEscChar(UkKeyEvent &ev);

protected:
    static bool m_classInit;
    CheckKeyboardCaseCb m_keyCheckFunc;
    UkSharedMem *m_pCtrl;

    int m_changePos;
    int m_backs;
    int m_bufSize;
    int m_current;
    int m_singleMode;

    int m_keyBufSize;
    // unsigned int m_keyStrokes[MAX_UK_ENGINE];
    KeyBufEntry m_keyStrokes[MAX_UK_ENGINE];
    int m_keyCurrent;
    bool m_toEscape;

    // variables valid in one session
    unsigned char *m_pOutBuf;
    int *m_pOutSize;
    bool m_outputWritten;
    bool m_reverted;
    bool m_keyRestored;
    bool m_keyRestoring;
    UkOutputType m_outType;

    struct WordInfo {
        // info for word ending at this position
        VnWordForm form;
        int c1Offset, vOffset, c2Offset;

        union {
            VowelSeq vseq;
            ConSeq cseq;
        };

        // info for current symbol
        int caps, tone;
        // canonical symbol, after caps, tone are removed
        // for non-Vn, vnSym == -1
        VnLexiName vnSym;
        int keyCode;
    };

    WordInfo m_buffer[MAX_UK_ENGINE];

    int processHookWithUO(UkKeyEvent &ev);
    int macroMatch(UkKeyEvent &ev);
    void markChange(int pos);
    void prepareBuffer(); // make sure we have a least 10 entries available
    int writeOutput(unsigned char *outBuf, int &outSize);
    // int getSeqLength(int first, int last);
    int getSeqSteps(int first, int last) const;
    int getTonePosition(VowelSeq vs, bool terminated) const;
    void resetKeyBuf();
    int checkEscapeVIQR(UkKeyEvent &ev);
    int processNoSpellCheck(UkKeyEvent &ev);
    int processWordEnd(UkKeyEvent &ev);
    void synchKeyStrokeBuffer();
    bool lastWordHasVnMark() const;
    bool lastWordIsNonVn() const;
};

void SetupUnikeyEngine();

#endif
