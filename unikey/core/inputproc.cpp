/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "inputproc.h"
#include <array>
#include <iostream>
#include <unordered_set>

using namespace std;

/*
unsigned char WordBreakSyms[] = {
    ',', ';', ':', '.', '\"', '\'', '!', '?', ' ',
    '<', '>', '=', '+', '-', '*', '/', '\\',
    '_', '~', '`', '@', '#', '$', '%', '^', '&', '(', ')', '{', '}', '[', ']'};
*/

const std::unordered_set<unsigned char> WordBreakSyms = {
    ',', ';', ':', '.', '\"', '\'', '!',  '?', ' ', '<',
    '>', '=', '+', '-', '*',  '/',  '\\', '_', '@', '#',
    '$', '%', '&', '(', ')',  '{',  '}',  '[', ']', '|'}; // we excluded ~, `, ^

VnLexiName AZLexiUpper[] = {vnl_A, vnl_B, vnl_C, vnl_D, vnl_E, vnl_F, vnl_G,
                            vnl_H, vnl_I, vnl_J, vnl_K, vnl_L, vnl_M, vnl_N,
                            vnl_O, vnl_P, vnl_Q, vnl_R, vnl_S, vnl_T, vnl_U,
                            vnl_V, vnl_W, vnl_X, vnl_Y, vnl_Z};

VnLexiName AZLexiLower[] = {vnl_a, vnl_b, vnl_c, vnl_d, vnl_e, vnl_f, vnl_g,
                            vnl_h, vnl_i, vnl_j, vnl_k, vnl_l, vnl_m, vnl_n,
                            vnl_o, vnl_p, vnl_q, vnl_r, vnl_s, vnl_t, vnl_u,
                            vnl_v, vnl_w, vnl_x, vnl_y, vnl_z};

UkCharType UkcMap[256];

struct _ascVnLexi {
    int asc;
    VnLexiName lexi;
};

// List of western characters outside range A-Z that are
// also Vietnamese characters
_ascVnLexi AscVnLexiList[] = {
    {0xC0, vnl_A2},       {0xC1, vnl_A1}, {0xC2, vnl_Ar}, {0xC2, vnl_A4},
    {0xC8, vnl_E2},       {0xC9, vnl_E1}, {0xCA, vnl_Er}, {0xCC, vnl_I2},
    {0xCD, vnl_I1},       {0xD2, vnl_O2}, {0xD3, vnl_O1}, {0xD4, vnl_Or},
    {0xD5, vnl_O4},       {0xD9, vnl_U2}, {0xDA, vnl_U1}, {0xDD, vnl_Y1},
    {0xE0, vnl_a2},       {0xE1, vnl_a1}, {0xE2, vnl_ar}, {0xE3, vnl_a4},
    {0xE8, vnl_e2},       {0xE9, vnl_e1}, {0xEA, vnl_er}, {0xEC, vnl_i2},
    {0xED, vnl_i1},       {0xF2, vnl_o2}, {0xF3, vnl_o1}, {0xF4, vnl_or},
    {0xF5, vnl_o4},       {0xF9, vnl_u2}, {0xFA, vnl_u1}, {0xFD, vnl_y1},
    {0x00, vnl_nonVnChar}};

VnLexiName IsoVnLexiMap[256];

bool ClassifierTableInitialized = false;

DllExport UkKeyMapping TelexMethodMapping[] = {{'Z', vneTone0},
                                               {'S', vneTone1},
                                               {'F', vneTone2},
                                               {'R', vneTone3},
                                               {'X', vneTone4},
                                               {'J', vneTone5},
                                               {'W', vne_telex_w},
                                               {'A', vneRoof_a},
                                               {'E', vneRoof_e},
                                               {'O', vneRoof_o},
                                               {'D', vneDd},
                                               {'[', vneCount + vnl_oh},
                                               {']', vneCount + vnl_uh},
                                               {'{', vneCount + vnl_Oh},
                                               {'}', vneCount + vnl_Uh},
                                               {0, vneNormal}};

DllExport UkKeyMapping SimpleTelexMethodMapping[] = {
    {'Z', vneTone0},  {'S', vneTone1},  {'F', vneTone2},   {'R', vneTone3},
    {'X', vneTone4},  {'J', vneTone5},  {'W', vneHookAll}, {'A', vneRoof_a},
    {'E', vneRoof_e}, {'O', vneRoof_o}, {'D', vneDd},      {0, vneNormal}};

DllExport UkKeyMapping SimpleTelex2MethodMapping[] = {
    {'Z', vneTone0},  {'S', vneTone1},  {'F', vneTone2},    {'R', vneTone3},
    {'X', vneTone4},  {'J', vneTone5},  {'W', vne_telex_w}, {'A', vneRoof_a},
    {'E', vneRoof_e}, {'O', vneRoof_o}, {'D', vneDd},       {0, vneNormal}};

DllExport UkKeyMapping VniMethodMapping[] = {
    {'0', vneTone0}, {'1', vneTone1}, {'2', vneTone2},   {'3', vneTone3},
    {'4', vneTone4}, {'5', vneTone5}, {'6', vneRoofAll}, {'7', vneHook_uo},
    {'8', vneBowl},  {'9', vneDd},    {0, vneNormal}};

DllExport UkKeyMapping VIQRMethodMapping[] = {
    {'0', vneTone0},   {'\'', vneTone1}, {'`', vneTone2},   {'?', vneTone3},
    {'~', vneTone4},   {'.', vneTone5},  {'^', vneRoofAll}, {'+', vneHook_uo},
    {'*', vneHook_uo}, {'(', vneBowl},   {'D', vneDd},      {'\\', vneEscChar},
    {0, vneNormal}};

DllExport UkKeyMapping MsViMethodMapping[] = {{'5', vneTone2},
                                              {'%', vneTone2},
                                              {'6', vneTone3},
                                              {'^', vneTone3},
                                              {'7', vneTone4},
                                              {'&', vneTone4},
                                              {'8', vneTone1},
                                              {'*', vneTone1},
                                              {'9', vneTone5},
                                              {'(', vneTone5},
                                              {'1', vneCount + vnl_ab},
                                              {'!', vneCount + vnl_Ab},
                                              {'2', vneCount + vnl_ar},
                                              {'@', vneCount + vnl_Ar},
                                              {'3', vneCount + vnl_er},
                                              {'#', vneCount + vnl_Er},
                                              {'4', vneCount + vnl_or},
                                              {'$', vneCount + vnl_Or},
                                              {'0', vneCount + vnl_dd},
                                              {')', vneCount + vnl_DD},
                                              {'[', vneCount + vnl_uh},
                                              {']', vneCount + vnl_oh},
                                              {'{', vneCount + vnl_Uh},
                                              {'}', vneCount + vnl_Oh},
                                              {0, vneNormal}};

//-------------------------------------------
void SetupInputClassifierTable() {
    if (!ClassifierTableInitialized) {
        ClassifierTableInitialized = true;
    }
    unsigned int c;
    int i;

    for (c = 0; c <= 32; c++) {
        UkcMap[c] = ukcReset;
    }

    for (c = 33; c < 256; c++) {
        UkcMap[c] = ukcNonVn;
    }

    /*
    for (c = '0'; c <= '9'; c++)
      UkcMap[c] = ukcNonVn;
    */

    for (c = 'a'; c <= 'z'; c++)
        UkcMap[c] = ukcVn;
    for (c = 'A'; c <= 'Z'; c++)
        UkcMap[c] = ukcVn;

    for (i = 0; AscVnLexiList[i].asc; i++) {
        UkcMap[AscVnLexiList[i].asc] = ukcVn;
    }

    UkcMap[(unsigned char)'j'] = ukcNonVn;
    UkcMap[(unsigned char)'J'] = ukcNonVn;
    UkcMap[(unsigned char)'f'] = ukcNonVn;
    UkcMap[(unsigned char)'F'] = ukcNonVn;
    UkcMap[(unsigned char)'w'] = ukcNonVn;
    UkcMap[(unsigned char)'W'] = ukcNonVn;

    for (auto wordBreakSym : WordBreakSyms)
        UkcMap[wordBreakSym] = ukcWordBreak;

    // Calculate IsoVnLexiMap
    for (i = 0; i < 256; i++) {
        IsoVnLexiMap[i] = vnl_nonVnChar;
    }

    for (i = 0; AscVnLexiList[i].asc; i++) {
        IsoVnLexiMap[AscVnLexiList[i].asc] = AscVnLexiList[i].lexi;
    }

    for (c = 'a'; c <= 'z'; c++) {
        IsoVnLexiMap[c] = AZLexiLower[c - 'a'];
    }

    for (c = 'A'; c <= 'Z'; c++) {
        IsoVnLexiMap[c] = AZLexiUpper[c - 'A'];
    }
}

//-------------------------------------------
void UkInputProcessor::init() {
    SetupInputClassifierTable();
    setIM(UkTelex);
}

//-------------------------------------------
int UkInputProcessor::setIM(UkInputMethod im) {
    m_im = im;
    switch (im) {
    case UkTelex:
        useBuiltIn(TelexMethodMapping);
        break;
    case UkSimpleTelex:
        useBuiltIn(SimpleTelexMethodMapping);
        break;
    case UkSimpleTelex2:
        useBuiltIn(SimpleTelex2MethodMapping);
        break;
    case UkVni:
        useBuiltIn(VniMethodMapping);
        break;
    case UkViqr:
        useBuiltIn(VIQRMethodMapping);
        break;
    case UkMsVi:
        useBuiltIn(MsViMethodMapping);
        break;
    default:
        m_im = UkTelex;
        useBuiltIn(TelexMethodMapping);
    }
    return 1;
}

//-------------------------------------------
int UkInputProcessor::setIM(int map[256]) {
    int i;
    m_im = UkUsrIM;
    for (i = 0; i < 256; i++)
        m_keyMap[i] = map[i];
    return 1;
}

//-------------------------------------------
void UkResetKeyMap(int keyMap[256]) {
    unsigned int c;
    for (c = 0; c < 256; c++)
        keyMap[c] = vneNormal;
}

//-------------------------------------------
void UkInputProcessor::useBuiltIn(UkKeyMapping *map) {
    UkResetKeyMap(m_keyMap);
    for (int i = 0; map[i].key; i++) {
        m_keyMap[map[i].key] = map[i].action;
        if (map[i].action < vneCount) {
            if (islower(map[i].key)) {
                m_keyMap[toupper(map[i].key)] = map[i].action;
            } else if (isupper(map[i].key)) {
                m_keyMap[tolower(map[i].key)] = map[i].action;
            }
        }
    }
}

//-------------------------------------------
void UkInputProcessor::keyCodeToEvent(unsigned int keyCode, UkKeyEvent &ev) {
    ev.keyCode = keyCode;
    if (keyCode == 0) {
        ev.evType = vneNormal;
        ev.vnSym = vnl_nonVnChar;
        ev.chType = ukcWordBreak;
    } else if (keyCode > 255) {
        ev.evType = vneNormal;
        ev.vnSym = IsoToVnLexi(keyCode);
        ev.chType = (ev.vnSym == vnl_nonVnChar) ? ukcNonVn : ukcVn;
    } else {
        ev.chType = UkcMap[keyCode];
        ev.evType = m_keyMap[keyCode];

        if (ev.evType >= vneTone0 && ev.evType <= vneTone5) {
            ev.tone = ev.evType - vneTone0;
        }

        if (ev.evType >= vneCount) {
            ev.chType = ukcVn;
            ev.vnSym = (VnLexiName)(ev.evType - vneCount);
            ev.evType = vneMapChar;
        } else {
            ev.vnSym = IsoToVnLexi(keyCode);
        }
    }
}

//----------------------------------------------------------------
// This method translates a key stroke to a symbol.
// Key strokes are simply considered character input, not action keys as in
// keyCodeToEvent method
//----------------------------------------------------------------
void UkInputProcessor::keyCodeToSymbol(unsigned int keyCode, UkKeyEvent &ev) {
    ev.keyCode = keyCode;
    ev.evType = vneNormal;
    ev.vnSym = IsoToVnLexi(keyCode);
    if (keyCode > 255) {
        ev.chType = (ev.vnSym == vnl_nonVnChar) ? ukcNonVn : ukcVn;
    } else {
        ev.chType = UkcMap[keyCode];
    }
}

//-------------------------------------------
UkCharType UkInputProcessor::getCharType(unsigned int keyCode) const {
    if (keyCode > 255)
        return (IsoToVnLexi(keyCode) == vnl_nonVnChar) ? ukcNonVn : ukcVn;
    return UkcMap[keyCode];
}

//-------------------------------------------
void UkInputProcessor::getKeyMap(int map[256]) const {
    int i;
    for (i = 0; i < 256; i++)
        map[i] = m_keyMap[i];
}
