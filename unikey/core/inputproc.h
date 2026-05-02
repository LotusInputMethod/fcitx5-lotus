/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef __UK_INPUT_PROCESSOR_H
#define __UK_INPUT_PROCESSOR_H

#include "keycons.h"
#include "vnlexi.h"
#include <unordered_set>

#if defined(_WIN32)
#define DllExport __declspec(dllexport)
#define DllImport __declspec(dllimport)
#if defined(UNIKEYHOOK)
#define DllInterface __declspec(dllexport)
#else
#define DllInterface __declspec(dllimport)
#endif
#else
#define DllInterface // not used
#define DllExport
#define DllImport
#endif

enum UkKeyEvName {
    vneRoofAll,
    vneRoof_a,
    vneRoof_e,
    vneRoof_o,
    vneHookAll,
    vneHook_uo,
    vneHook_u,
    vneHook_o,
    vneBowl,
    vneDd,
    vneTone0,
    vneTone1,
    vneTone2,
    vneTone3,
    vneTone4,
    vneTone5,
    vne_telex_w, // special for telex
    vneMapChar,  // e.g. [ -> u+ , ] -> o+
    vneEscChar,
    vneNormal, // does not belong to any of the above categories
    vneCount   // just to count how many event types there are
};

enum UkCharType { ukcVn, ukcWordBreak, ukcNonVn, ukcReset };

struct UkKeyEvent {
    int evType;
    UkCharType chType;
    VnLexiName vnSym; // meaningful only when chType==ukcVn
    unsigned int keyCode;
    int tone; // meaningful only when this is a vowel
};

struct UkKeyMapping {
    unsigned char key;
    int action;
};

///////////////////////////////////////////
class UkInputProcessor {

public:
    // don't do anything with constructor, because
    // this object can be allocated in shared memory
    // Use init method instead
    // UkInputProcessor();

    void init();

    UkInputMethod getIM() const { return m_im; }

    void keyCodeToEvent(unsigned int keyCode, UkKeyEvent &ev);
    void keyCodeToSymbol(unsigned int keyCode, UkKeyEvent &ev);
    int setIM(UkInputMethod im);
    int setIM(int map[256]);
    void getKeyMap(int map[256]) const;

    UkCharType getCharType(unsigned int keyCode) const;

protected:
    static bool m_classInit;

    UkInputMethod m_im;
    int m_keyMap[256];

    void useBuiltIn(UkKeyMapping *map);
};

void UkResetKeyMap(int keyMap[256]);
void SetupInputClassifierTable();

DllInterface extern UkKeyMapping TelexMethodMapping[];
DllInterface extern UkKeyMapping SimpleTelexMethodMapping[];
DllInterface extern UkKeyMapping SimpleTelex2MethodMapping[];
DllInterface extern UkKeyMapping VniMethodMapping[];
DllInterface extern UkKeyMapping VIQRMethodMapping[];
DllInterface extern UkKeyMapping MsViMethodMapping[];

extern VnLexiName IsoVnLexiMap[];
inline VnLexiName IsoToVnLexi(unsigned int keyCode) {
    return (keyCode >= 256) ? vnl_nonVnChar : IsoVnLexiMap[keyCode];
}

extern const std::unordered_set<unsigned char> WordBreakSyms;

#endif
