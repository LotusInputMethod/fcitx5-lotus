/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "mactab.h"
#include "vnconv.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;
#define UKMACRO_VERSION_UTF8 1

//---------------------------------------------------------------
void CMacroTable::init() {
    m_memSize = MACRO_MEM_SIZE;
    m_count = 0;
    m_occupied = 0;
}

//---------------------------------------------------------------
char *MacCompareStartMem;

#define STD_TO_LOWER(x)                                                        \
    (((x) >= VnStdCharOffset &&                                                \
      (x) < (VnStdCharOffset + TOTAL_ALPHA_VNCHARS) && !((x) & 1))             \
         ? (x + 1)                                                             \
         : (x))

int macCompare(const void *p1, const void *p2) {
    StdVnChar *s1 =
        (StdVnChar *)((char *)MacCompareStartMem + ((MacroDef *)p1)->keyOffset);
    StdVnChar *s2 =
        (StdVnChar *)((char *)MacCompareStartMem + ((MacroDef *)p2)->keyOffset);

    int i;
    StdVnChar ls1, ls2;

    for (i = 0; s1[i] != 0 && s2[i] != 0; i++) {
        ls1 = STD_TO_LOWER(s1[i]);
        ls2 = STD_TO_LOWER(s2[i]);
        if (ls1 > ls2)
            return 1;
        if (ls1 < ls2)
            return -1;
        /*
        if (s1[i] > s2[i])
            return 1;
        if (s1[i] < s2[i])
            return -1;
        */
    }
    if (s1[i] == 0)
        return (s2[i] == 0) ? 0 : -1;
    return 1;
}

//---------------------------------------------------------------
int macKeyCompare(const void *key, const void *ele) {
    StdVnChar *s1 = (StdVnChar *)key;
    StdVnChar *s2 = (StdVnChar *)((char *)MacCompareStartMem +
                                  ((MacroDef *)ele)->keyOffset);

    StdVnChar ls1, ls2;
    int i;
    for (i = 0; s1[i] != 0 && s2[i] != 0; i++) {
        ls1 = STD_TO_LOWER(s1[i]);
        ls2 = STD_TO_LOWER(s2[i]);
        if (ls1 > ls2)
            return 1;
        if (ls1 < ls2)
            return -1;
        /*
        if (s1[i] > s2[i])
            return 1;
        if (s1[i] < s2[i])
            return -1;
        */
    }
    if (s1[i] == 0)
        return (s2[i] == 0) ? 0 : -1;
    return 1;
}

//---------------------------------------------------------------
const StdVnChar *CMacroTable::lookup(StdVnChar *key) {
    MacCompareStartMem = m_macroMem;
    MacroDef *p = (MacroDef *)bsearch(key, m_table, m_count, sizeof(MacroDef),
                                      macKeyCompare);
    if (p)
        return (StdVnChar *)(m_macroMem + p->textOffset);
    return 0;
}

//----------------------------------------------------------------------------
// Read header, if it's present in the file. Get the version of the file
// If header is absent, go back to the beginning of file and set version to 0
// Return false if reading failed.
//
// Header format: ;[DO NOT DELETE THIS LINE]***version=n
//----------------------------------------------------------------------------
bool CMacroTable::readHeader(FILE *f, int &version) {
    char line[MAX_MACRO_LINE];
    if (!fgets(line, sizeof(line), f)) {
        if (feof(f)) {
            fseek(f, 0, SEEK_SET);
            version = 0;
            return true;
        }
        return false;
    }

    // if BOM is available, skip it
    char *p = line;
    size_t len = strlen(line);
    if (len >= 3 && (unsigned char)line[0] == 0xEF &&
        (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
        p += 3;
    }

    // read version number
    p = strstr(p, "***");
    if (p) {
        p += 3;
        // skip possible spaces
        while (*p == ' ')
            p++;
        if (sscanf(p, "version=%d", &version) == 1)
            return true;
    }

    fseek(f, 0, SEEK_SET);
    version = 0;
    return true;
}

//----------------------------------------------------------------
void CMacroTable::writeHeader(FILE *f) {
#if defined(WIN32)
    fprintf(f, "\xEF\xBB\xBF;DO NOT DELETE THIS LINE*** version=%d ***\n",
            UKMACRO_VERSION_UTF8);
#else
    fprintf(f, "DO NOT DELETE THIS LINE*** version=%d ***\n",
            UKMACRO_VERSION_UTF8);
#endif
}
//---------------------------------------------------------------
int CMacroTable::loadFromFile(const char *fname) {
    FILE *f;
#if defined(WIN32)
    f = _tfopen(fname, _TEXT("rt"));
#else
    f = fopen(fname, "r");
#endif

    if (f == NULL)
        return 0;
    char line[MAX_MACRO_LINE];
    size_t len;

    resetContent();

    // read possible header
    int version;
    if (!readHeader(f, version)) {
        version = 0;
    }

    while (fgets(line, sizeof(line), f)) {
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = 0;
        if (len > 1 && line[len - 2] == '\r')
            line[len - 2] = 0;
        if (version == UKMACRO_VERSION_UTF8)
            addItem(line, CONV_CHARSET_UNIUTF8);
        else
            addItem(line, CONV_CHARSET_VIQR);
    }
    fclose(f);
    MacCompareStartMem = m_macroMem;
    qsort(m_table, m_count, sizeof(MacroDef), macCompare);
    // Convert old version
    if (version != UKMACRO_VERSION_UTF8) {
        writeToFile(fname);
    }
    return 1;
}

//---------------------------------------------------------------
int CMacroTable::writeToFile(const char *fname) {
    FILE *f;
    f = fopen(fname, "w");
    return writeToFp(f);
}

int CMacroTable::writeToFp(FILE *f) {
    int ret;
    int inLen, maxOutLen;

    if (f == NULL)
        return 0;

    char line[MAX_MACRO_LINE * 3 + 1]; // 1 VnChar may need 3 chars in UTF8
    char key[MAX_MACRO_KEY_LEN * 3];
    char text[MAX_MACRO_TEXT_LEN * 3];

    writeHeader(f);

    UKBYTE *p;
    for (int i = 0; i < m_count; i++) {
        p = (UKBYTE *)m_macroMem + m_table[i].keyOffset;
        inLen = -1;
        maxOutLen = sizeof(key);
        ret = VnConvert(CONV_CHARSET_VNSTANDARD, CONV_CHARSET_UNIUTF8,
                        (UKBYTE *)p, (UKBYTE *)key, &inLen, &maxOutLen);
        if (ret != 0)
            continue;

        p = (UKBYTE *)m_macroMem + m_table[i].textOffset;
        inLen = -1;
        maxOutLen = sizeof(text);
        ret = VnConvert(CONV_CHARSET_VNSTANDARD, CONV_CHARSET_UNIUTF8, p,
                        (UKBYTE *)text, &inLen, &maxOutLen);
        if (ret != 0)
            continue;
        if (i < m_count - 1)
            sprintf(line, "%s:%s\n", key, text);
        else
            sprintf(line, "%s:%s", key, text);
        fputs(line, f);
    }

    fclose(f);
    return 1;
}

//---------------------------------------------------------------
int CMacroTable::addItem(const void *key, const void *text, int charset) {
    int ret;
    int inLen, maxOutLen;
    int offset = m_occupied;
    char *p = m_macroMem + offset;

    if (m_count >= MAX_MACRO_ITEMS)
        return -1;

    m_table[m_count].keyOffset = offset;

    // Convert macro key to VN standard
    inLen = -1; // input is null-terminated
    maxOutLen = MAX_MACRO_KEY_LEN * sizeof(StdVnChar);
    if (maxOutLen + offset > m_memSize)
        maxOutLen = m_memSize - offset;
    ret = VnConvert(charset, CONV_CHARSET_VNSTANDARD, (UKBYTE *)key,
                    (UKBYTE *)p, &inLen, &maxOutLen);
    if (ret != 0)
        return -1;

    offset += maxOutLen;
    p += maxOutLen;

    // convert macro text to VN standard
    m_table[m_count].textOffset = offset;
    inLen = -1; // input is null-terminated
    maxOutLen = MAX_MACRO_TEXT_LEN * sizeof(StdVnChar);
    if (maxOutLen + offset > m_memSize)
        maxOutLen = m_memSize - offset;
    ret = VnConvert(charset, CONV_CHARSET_VNSTANDARD, (UKBYTE *)text,
                    (UKBYTE *)p, &inLen, &maxOutLen);
    if (ret != 0)
        return -1;

    m_occupied = offset + maxOutLen;
    m_count++;
    return (m_count - 1);
}

//---------------------------------------------------------------
// add a new macro into the sorted macro table
// item format: key:text (key and text are separated by a colon)
//---------------------------------------------------------------
int CMacroTable::addItem(const char *item, int charset) {
    char key[MAX_MACRO_KEY_LEN];

    // Parse the input item
    char *pos = (char *)strchr(item, ':');
    if (pos == NULL)
        return -1;
    int keyLen = (int)(pos - item);
    if (keyLen > MAX_MACRO_KEY_LEN - 1)
        keyLen = MAX_MACRO_KEY_LEN - 1;
    strncpy(key, item, keyLen);
    key[keyLen] = '\0';
    return addItem(key, ++pos, charset);
}

//---------------------------------------------------------------
void CMacroTable::resetContent() {
    m_occupied = 0;
    m_count = 0;
}

//---------------------------------------------------------------
const StdVnChar *CMacroTable::getKey(int idx) const {
    if (idx < 0 || idx >= m_count)
        return 0;
    return (StdVnChar *)(m_macroMem + m_table[idx].keyOffset);
}

//---------------------------------------------------------------
const StdVnChar *CMacroTable::getText(int idx) const {
    if (idx < 0 || idx >= m_count)
        return 0;
    return (StdVnChar *)(m_macroMem + m_table[idx].textOffset);
}
