/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __MACRO_TABLE_H
#define __MACRO_TABLE_H

#include "charset.h"
#include "keycons.h"

#if defined(_WIN32)
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

struct MacroDef {
    int keyOffset;
    int textOffset;
};

#if !defined(WIN32)
typedef char TCHAR;
#endif

class DllInterface CMacroTable {
public:
    void init();
    int loadFromFile(const char *fname);
    int writeToFile(const char *fname);
    int writeToFp(FILE *f);

    const StdVnChar *lookup(StdVnChar *key);
    const StdVnChar *getKey(int idx) const;
    const StdVnChar *getText(int idx) const;
    int getCount() const { return m_count; }
    void resetContent();
    int addItem(const char *item, int charset);
    int addItem(const void *key, const void *text, int charset);

protected:
    bool readHeader(FILE *f, int &version);
    void writeHeader(FILE *f);

    MacroDef m_table[MAX_MACRO_ITEMS];
    char m_macroMem[MACRO_MEM_SIZE];

    int m_count;
    int m_memSize, m_occupied;
};

#endif
