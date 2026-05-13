/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "byteio.h"
#include <string.h>

//------------------------------------------------
StringBIStream::StringBIStream(UKBYTE *data, int len, int elementSize) {
    m_data = m_current = data;
    m_len = m_left = len;
    if (len == -1) {
        if (elementSize == 2)
            m_eos = (*(UKWORD *)data == 0);
        else if (elementSize == 4)
            m_eos = (*(UKDWORD *)data == 4);
        else
            m_eos = (*data == 0);
    } else
        m_eos = (len <= 0);
    m_didBookmark = 0;
}

//------------------------------------------------
int StringBIStream::eos() { return m_eos; }

//------------------------------------------------
int StringBIStream::getNext(UKBYTE &b) {
    if (m_eos)
        return 0;
    b = *m_current++;
    if (m_len == -1) {
        m_eos = (b == 0);
    } else {
        m_left--;
        m_eos = (m_left <= 0);
    }
    return 1;
}

//------------------------------------------------
int StringBIStream::unget(UKBYTE b) {
    if (m_current != m_data) {
        *--m_current = b;
        m_eos = 0;
        if (m_len != -1)
            m_left++;
    }
    return 1;
}

//------------------------------------------------
int StringBIStream::getNextW(UKWORD &w) {
    if (m_eos)
        return 0;
    w = *((UKWORD *)m_current);
    m_current += 2;
    if (m_len == -1)
        m_eos = (w == 0);
    else {
        m_left -= 2;
        m_eos = (m_left <= 0);
    }
    return 1;
}

//------------------------------------------------
int StringBIStream::getNextDW(UKDWORD &dw) {
    if (m_eos)
        return 0;

    dw = *((UKDWORD *)m_current);
    m_current += 4;
    if (m_len == -1)
        m_eos = (dw == 0);
    else {
        m_left -= 4;
        m_eos = (m_left <= 0);
    }
    return 1;
}

//------------------------------------------------
int StringBIStream::peekNext(UKBYTE &b) {
    if (m_eos)
        return 0;
    b = *m_current;
    return 1;
}

//------------------------------------------------
int StringBIStream::peekNextW(UKWORD &w) {
    if (m_eos)
        return 0;
    w = *((UKWORD *)m_current);
    return 1;
}

/*
//------------------------------------------------
int StringBIStream::peekNextDW(UKDWORD & dw)
{
    if (m_eos)
        return 0;
    dw = *((UKDWORD *)m_current);
    return 1;
}
*/

//------------------------------------------------
void StringBIStream::reopen() {
    m_current = m_data;
    m_left = m_len;
    if (m_len == -1)
        m_eos = (m_data == 0);
    else
        m_eos = (m_len <= 0);
    m_didBookmark = 0;
}

//------------------------------------------------
int StringBIStream::bookmark() {
    m_didBookmark = 1;
    m_bookmark.current = m_current;
    m_bookmark.data = m_data;
    m_bookmark.eos = m_eos;
    m_bookmark.left = m_left;
    m_bookmark.len = m_len;
    return 1;
}

//------------------------------------------------
int StringBIStream::gotoBookmark() {
    if (!m_didBookmark)
        return 0;
    m_current = m_bookmark.current;
    m_data = m_bookmark.data;
    m_eos = m_bookmark.eos;
    m_left = m_bookmark.left;
    m_len = m_bookmark.len;
    return 1;
}

//------------------------------------------------
int StringBIStream::close() { return 1; };

//////////////////////////////////////////////////
// Class StringBOStream
//////////////////////////////////////////////////

//------------------------------------------------
StringBOStream::StringBOStream(UKBYTE *buf, int len) {
    m_current = m_buf = buf;
    m_len = len;
    m_out = 0;
    m_bad = 0;
}

//------------------------------------------------
int StringBOStream::putB(UKBYTE b) {
    m_out++;
    /*
        if (m_out >= 2147483647) {
            int err;
            err = 1;
        }
    */
    if (m_bad)
        return 0;
    /*
        if (m_out < 0) {
            int i;
            i = 1;
        }
    */
    if (m_out <= m_len) {
        *m_current++ = b;
        return 1;
    }
    m_bad = 1;
    return 0;
}

//------------------------------------------------
int StringBOStream::putW(UKWORD w) {
    m_out += 2;
    if (m_bad)
        return 0;
    if (m_out <= m_len) {
        *((UKWORD *)m_current) = w;
        m_current += 2;
        return 1;
    }
    m_bad = 1;
    return 0;
}

//------------------------------------------------
int StringBOStream::puts(const char *s, int size) {
    if (size == -1) {
        while (*s) {
            m_out++;
            if (m_out <= m_len)
                *m_current++ = *s;
            s++;
        }
        if (!m_bad && m_out > m_len)
            m_bad = 1;
        return (!m_bad);
    }

    int n;
    if (!m_bad && m_out <= m_len) {
        n = m_len - m_out;
        if (n > size)
            n = size;
        memcpy(m_current, s, n);
        m_current += n;
    }

    m_out += size;
    if (!m_bad && m_out > m_len)
        m_bad = 1;
    return (!m_bad);
}

//------------------------------------------------
void StringBOStream::reopen() {
    m_current = m_buf;
    m_out = 0;
    m_bad = 0;
}

//------------------------------------------------
int StringBOStream::isOK() { return !m_bad; }

////////////////////////////////////////////////////
// Class FileBIStream                             //
////////////////////////////////////////////////////

//----------------------------------------------------
FileBIStream::FileBIStream(int bufSize, char *buf) {
    m_file = NULL;
    m_buf = buf;
    m_bufSize = bufSize;
    m_own = 1;
    m_didBookmark = 0;

    m_readAhead = 0;
    m_lastIsAhead = 0;
}

//----------------------------------------------------
FileBIStream::~FileBIStream() {
    if (m_own)
        close();
}

//----------------------------------------------------
int FileBIStream::open(const char *fileName) {
    m_file = fopen(fileName, "rb");
    if (m_file == NULL)
        return 0;
    setvbuf(m_file, m_buf, _IOFBF, m_bufSize);
    m_own = 0;
    m_readAhead = 0;
    m_lastIsAhead = 0;
    return 1;
}

//----------------------------------------------------
int FileBIStream::close() {
    if (m_file != NULL) {
        fclose(m_file);
        m_file = NULL;
    }
    return 1;
}

//----------------------------------------------------
void FileBIStream::attach(FILE *f) {
    m_file = f;
    m_own = 0;
    m_readAhead = 0;
    m_lastIsAhead = 0;
}

//----------------------------------------------------
int FileBIStream::eos() {
    if (m_readAhead)
        return 0;
    return feof(m_file);
}

//----------------------------------------------------
int FileBIStream::getNext(UKBYTE &b) {
    if (m_readAhead) {
        m_readAhead = 0;
        b = m_readByte;
        m_lastIsAhead = 1;
        return 1;
    }

    m_lastIsAhead = 0;
    b = fgetc(m_file);
    return (!feof(m_file));
}

//----------------------------------------------------
int FileBIStream::peekNext(UKBYTE &b) {
    if (m_readAhead) {
        b = m_readByte;
        return 1;
    }

    b = fgetc(m_file);
    if (feof(m_file))
        return 0;
    ungetc(b, m_file);
    return 1;
}

//----------------------------------------------------
int FileBIStream::unget(UKBYTE b) {
    if (m_lastIsAhead) {
        m_lastIsAhead = 0;
        m_readAhead = 1;
        m_readByte = b;
        return 1;
    }

    ungetc(b, m_file);
    return 1;
}

//----------------------------------------------------
int FileBIStream::getNextW(UKWORD &w) {
    UKBYTE b1, b2;

    if (getNext(b1)) {
        if (getNext(b2)) {
            *((UKBYTE *)&w) = b1;
            *(((UKBYTE *)&w) + 1) = b2;
            return 1;
        }
    }
    return 0;
}

//----------------------------------------------------
int FileBIStream::getNextDW(UKDWORD &dw) {
    UKWORD w1, w2;
    if (getNextW(w1)) {
        if (getNextW(w2)) {
            *((UKWORD *)&dw) = w1;
            *(((UKWORD *)&dw) + 1) = w2;
            return 1;
        }
    }
    return 0;
}
//----------------------------------------------------
int FileBIStream::peekNextW(UKWORD &w) {
    UKBYTE hi, low;
    if (getNext(low)) {
        if (getNext(hi)) {
            unget(hi);
            w = hi;
            w = (w << 8) + low;
            m_readAhead = 1;
            m_readByte = low;
            m_lastIsAhead = 0;
            return 1;
        }

        m_readAhead = 1;
        m_readByte = low;
        m_lastIsAhead = 0;
        return 0;
    }
    return 0;
}

//----------------------------------------------------
int FileBIStream::bookmark() {
    m_didBookmark = 1;
    m_bookmark.pos = ftell(m_file);
    return 1;
}

//----------------------------------------------------
int FileBIStream::gotoBookmark() {
    if (!m_didBookmark)
        return 0;
    fseek(m_file, m_bookmark.pos, SEEK_SET);
    return 1;
}

////////////////////////////////////////////////////
// Class FileBOStream                             //
////////////////////////////////////////////////////
//----------------------------------------------------
FileBOStream::FileBOStream(int bufSize, char *buf) {
    m_file = NULL;
    m_buf = buf;
    m_bufSize = bufSize;
    m_own = 1;
    m_bad = 1;
}

//----------------------------------------------------
FileBOStream::~FileBOStream() {
    if (m_own)
        close();
}

//----------------------------------------------------
int FileBOStream::open(const char *fileName) {
    m_file = fopen(fileName, "wb");
    if (m_file == NULL)
        return 0;
    m_bad = 0;
    setvbuf(m_file, m_buf, _IOFBF, m_bufSize);
    m_own = 1;
    return 1;
}

//----------------------------------------------------
void FileBOStream::attach(FILE *f) {
    m_file = f;
    m_own = 0;
    m_bad = 0;
}

//----------------------------------------------------
int FileBOStream::close() {
    if (m_file != NULL) {
        fclose(m_file);
        m_file = NULL;
    }
    return 1;
}

//----------------------------------------------------
int FileBOStream::putB(UKBYTE b) {
    if (m_bad)
        return 0;
    m_bad = (fputc(b, m_file) == EOF);
    return (!m_bad);
}

//----------------------------------------------------
int FileBOStream::putW(UKWORD w) {
    if (m_bad)
        return 0;
    //	m_bad = (fputwc(w, m_file) == WEOF);
    m_bad = (fputc((UKBYTE)w, m_file) == EOF);
    if (m_bad)
        return 0;
    m_bad = (fputc((UKBYTE)(w >> 8), m_file) == EOF);
    return (!m_bad);
}

//----------------------------------------------------
int FileBOStream::puts(const char *s, int size) {
    if (m_bad)
        return 0;
    if (size == -1) {
        m_bad = (fputs(s, m_file) == EOF);
        return (!m_bad);
    }
    int out = fwrite(s, 1, size, m_file);
    m_bad = (out != size);
    return (!m_bad);
}

//----------------------------------------------------
int FileBOStream::isOK() { return !m_bad; }
