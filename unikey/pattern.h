/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <longp@cslab.felk.cvut.cz>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __PATTERN_H
#define __PATTERN_H

#if defined(_WIN32)
#if defined(UNIKEYHOOK)
#define DllInterface __declspec(dllexport)
#else
#define DllInterface __declspec(dllimport)
#endif
#else
#define DllInterface // not used
#endif

#define MAX_PATTERN_LEN 40

class DllInterface PatternState {
public:
    char *m_pattern;
    int m_border[MAX_PATTERN_LEN + 1];
    int m_pos;
    int m_found;
    void init(char *pattern);
    void reset();
    int foundAtNextChar(
        char ch); // get next input char, returns 1 if pattern is found.
};

class DllInterface PatternList {
public:
    PatternState *m_patterns;
    int m_count;
    void init(char **patterns, int count);
    int foundAtNextChar(char ch);
    void reset();

    PatternList() {
        m_count = 0;
        m_patterns = 0;
    }

    ~PatternList() {
        if (m_patterns)
            delete[] m_patterns;
    }
};

#endif
