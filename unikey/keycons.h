/*
 * SPDX-FileCopyrightText: 1998-2004 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __KEY_CONS_H
#define __KEY_CONS_H

// macro table constants
#define MAX_MACRO_KEY_LEN 16
// #define MAX_MACRO_TEXT_LEN 256
#define MAX_MACRO_TEXT_LEN 1024
#define MAX_MACRO_ITEMS 1024
#define MAX_MACRO_LINE (MAX_MACRO_TEXT_LEN + MAX_MACRO_KEY_LEN)

#define MACRO_MEM_SIZE (1024 * 128) // 128 KB

#define CP_US_ANSI 1252

enum UkInputMethod {
    UkTelex,
    UkVni,
    UkViqr,
    UkMsVi,
    UkUsrIM,
    UkSimpleTelex,
    UkSimpleTelex2
};

struct UnikeyOptions {
    int freeMarking;
    int modernStyle;
    int macroEnabled;
    int useUnicodeClipboard;
    int alwaysMacro;
    int strictSpellCheck;
    int useIME; // for Win32 only
    int spellCheckEnabled;
    int autoNonVnRestore;
};

#define UKOPT_FLAG_ALL 0xFFFFFFFF
#define UKOPT_FLAG_FREE_STYLE 0x00000001
// #define UKOPT_FLAG_MANUAL_TONE           0x00000002
#define UKOPT_FLAG_MODERN 0x00000004
#define UKOPT_FLAG_MACRO_ENABLED 0x00000008
#define UKOPT_FLAG_USE_CLIPBOARD 0x00000010
#define UKOPT_FLAG_ALWAYS_MACRO 0x00000020
#define UKOPT_FLAG_STRICT_SPELL 0x00000040
#define UKOPT_FLAG_USE_IME 0x00000080
#define UKOPT_FLAG_SPELLCHECK_ENABLED 0x00000100

#if defined(WIN32)
typedef struct _UnikeySysInfo UnikeySysInfo;
struct _UnikeySysInfo {
    int switchKey;
    HHOOK keyHook;
    HHOOK mouseHook;
    HWND hMainDlg;
    UINT iconMsgId;
    HICON hVietIcon, hEnIcon;
    int unicodePlatform;
    DWORD winMajorVersion, winMinorVersion;
};
#endif

typedef enum { UkCharOutput, UkKeyOutput } UkOutputType;

#endif
