/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ctype.h>
#include <memory.h>
#include <search.h>
#include <stddef.h>
#include <stdlib.h>

#include "charset.h"
#include "data.h"

int LoVowel['z' - 'a' + 1];
int HiVowel['Z' - 'A' + 1];

#define IS_VOWEL(x)                                                            \
    ((x >= 'a' && x <= 'z' && LoVowel[x - 'a']) ||                             \
     (x >= 'A' && x <= 'Z' && HiVowel[x - 'A']))

SingleByteCharset *SgCharsets[CONV_TOTAL_SINGLE_CHARSETS];
DoubleByteCharset *DbCharsets[CONV_TOTAL_DOUBLE_CHARSETS];

DllExport CVnCharsetLib VnCharsetLibObj;

//////////////////////////////////////////////////////
// Generic VnCharset class
//////////////////////////////////////////////////////
int VnCharset::elementSize() { return 1; }

//-------------------------------------------
int VnInternalCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                 int &bytesRead) {
    if (!is.getNextDW(stdChar)) {
        bytesRead = 0;
        return 0;
    }
    bytesRead = sizeof(UKDWORD);
    return 1;
}

//-------------------------------------------
int VnInternalCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                               int &outLen) {
    outLen = sizeof(StdVnChar);
    os.putW((UKWORD)stdChar);
    return os.putW((UKWORD)(stdChar >> (sizeof(UKWORD) * 8)));
}

//-------------------------------------------
int VnInternalCharset::elementSize() { return 4; }

//-------------------------------------------
SingleByteCharset::SingleByteCharset(unsigned char *vnChars) {
    int i;
    m_vnChars = vnChars;
    memset(m_stdMap, 0, 256 * sizeof(UKWORD));
    for (i = 0; i < TOTAL_VNCHARS; i++) {
        if (vnChars[i] != 0 &&
            (i == TOTAL_VNCHARS - 1 || vnChars[i] != vnChars[i + 1]))
            m_stdMap[vnChars[i]] = i + 1;
    }
}

//-------------------------------------------
int SingleByteCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                 int &bytesRead) {
    unsigned char ch;
    if (!is.getNext(ch)) {
        bytesRead = 0;
        return 0;
    }

    stdChar = (m_stdMap[ch]) ? (VnStdCharOffset + m_stdMap[ch] - 1) : ch;
    bytesRead = 1;
    return 1;
}

//-------------------------------------------
int SingleByteCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                               int &outLen) {
    int ret;
    unsigned char ch;
    if (stdChar >= VnStdCharOffset) {
        outLen = 1;
        ch = m_vnChars[stdChar - VnStdCharOffset];
        if (ch == 0)
            ch = (stdChar == StdStartQuote)
                     ? PadStartQuote
                     : ((stdChar == StdEndQuote)
                            ? PadEndQuote
                            : ((stdChar == StdEllipsis) ? PadEllipsis
                                                        : PadChar));
        ret = os.putB(ch);
    } else {
        if (stdChar > 255 || m_stdMap[stdChar]) {
            // this character is missing in the charset
            // output padding character
            outLen = 1;
            ret = os.putB(PadChar);
        } else {
            outLen = 1;
            ret = os.putB((UKBYTE)stdChar);
        }
    }
    return ret;
}

//-------------------------------------------
int wideCharCompare(const void *ele1, const void *ele2) {
    UKWORD ch1 = LOWORD(*((UKDWORD *)ele1));
    UKWORD ch2 = LOWORD(*((UKDWORD *)ele2));
    return (ch1 == ch2) ? 0 : ((ch1 > ch2) ? 1 : -1);
}

//-------------------------------------------
UnicodeCharset::UnicodeCharset(UnicodeChar *vnChars) {
    UKDWORD i;
    m_toUnicode = vnChars;
    for (i = 0; i < TOTAL_VNCHARS; i++)
        m_vnChars[i] = (i << 16) + vnChars[i]; // high word is used for index
    qsort(m_vnChars, TOTAL_VNCHARS, sizeof(UKDWORD), wideCharCompare);
}

//-------------------------------------------
int UnicodeCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                              int &bytesRead) {
    UnicodeChar uniCh;
    if (!is.getNextW(uniCh)) {
        bytesRead = 0;
        return 0;
    }
    bytesRead = sizeof(UnicodeChar);
    UKDWORD key = uniCh;
    UKDWORD *pChar = (UKDWORD *)bsearch(&key, m_vnChars, TOTAL_VNCHARS,
                                        sizeof(UKDWORD), wideCharCompare);
    if (pChar)
        stdChar = VnStdCharOffset + HIWORD(*pChar);
    else
        stdChar = uniCh;
    return 1;
}

//-------------------------------------------
int UnicodeCharset::putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen) {
    outLen = sizeof(UnicodeChar);
    return os.putW((stdChar >= VnStdCharOffset)
                       ? m_toUnicode[stdChar - VnStdCharOffset]
                       : (UnicodeChar)stdChar);
}

//-------------------------------------------
int UnicodeCharset::elementSize() { return 2; }

////////////////////////////////////////
// Unicode decomposed
////////////////////////////////////////
//-------------------------------------------
int uniCompInfoCompare(const void *ele1, const void *ele2) {
    UKDWORD ch1 = ((UniCompCharInfo *)ele1)->compChar;
    UKDWORD ch2 = ((UniCompCharInfo *)ele2)->compChar;
    return (ch1 == ch2) ? 0 : ((ch1 > ch2) ? 1 : -1);
}

UnicodeCompCharset::UnicodeCompCharset(UnicodeChar *uniChars,
                                       UKDWORD *uniCompChars) {
    int i, k;
    m_uniCompChars = uniCompChars;
    m_totalChars = 0;
    for (i = 0; i < TOTAL_VNCHARS; i++) {
        m_info[i].compChar = uniCompChars[i];
        m_info[i].stdIndex = i;
        m_totalChars++;
    }

    for (k = 0, i = TOTAL_VNCHARS; k < TOTAL_VNCHARS; k++)
        if (uniChars[k] != uniCompChars[k]) {
            m_info[i].compChar = uniChars[k];
            m_info[i].stdIndex = k;
            m_totalChars++;
            i++;
        }

    qsort(m_info, m_totalChars, sizeof(UniCompCharInfo), uniCompInfoCompare);
}

//---------------------------------------------
int UnicodeCompCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                  int &bytesRead) {
    // read first char

    UniCompCharInfo key;
    UKWORD w;
    if (!is.getNextW(w)) {
        bytesRead = 0;
        return 0;
    }
    key.compChar = w;
    bytesRead = 2;

    UniCompCharInfo *pInfo =
        (UniCompCharInfo *)bsearch(&key, m_info, m_totalChars,
                                   sizeof(UniCompCharInfo), uniCompInfoCompare);
    if (!pInfo)
        stdChar = key.compChar;
    else {
        stdChar = pInfo->stdIndex + VnStdCharOffset;
        if (is.peekNextW(w)) {
            UKDWORD hi = w;
            if (hi > 0) {
                key.compChar += hi << 16;
                pInfo = (UniCompCharInfo *)bsearch(&key, m_info, m_totalChars,
                                                   sizeof(UniCompCharInfo),
                                                   uniCompInfoCompare);
                if (pInfo) {
                    stdChar = pInfo->stdIndex + VnStdCharOffset;
                    bytesRead += 2;
                    is.getNextW(w);
                }
            }
        }
    }
    return 1;
}

//---------------------------------------------
int UnicodeCompCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                                int &outLen) {
    int ret;
    if (stdChar >= VnStdCharOffset) {
        UKDWORD uniCompCh = m_uniCompChars[stdChar - VnStdCharOffset];
        UKWORD lo = LOWORD(uniCompCh);
        UKWORD hi = HIWORD(uniCompCh);
        outLen = 2;
        ret = os.putW(lo);
        if (hi > 0) {
            outLen += 2;
            ret = os.putW(hi);
        }
    } else {
        outLen = 2;
        ret = os.putW((UKWORD)stdChar);
    }
    return ret;
}

//-------------------------------------------
int UnicodeCompCharset::elementSize() { return 2; }

////////////////////////////////
// Unicode UTF-8              //
////////////////////////////////
int UnicodeUTF8Charset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                  int &bytesRead) {
    UKWORD w1, w2, w3;
    UKBYTE first, second, third;
    UnicodeChar uniCh;

    bytesRead = 0;
    if (!is.getNext(first))
        return 0;
    bytesRead = 1;

    if (first < 0x80)
        uniCh = first; // 1-byte sequence
    else if ((first & 0xE0) == 0xC0) {
        // 2-byte sequence
        if (!is.peekNext(second))
            return 0;
        if ((second & 0xC0) != 0x80) {
            stdChar = INVALID_STD_CHAR;
            return 1;
        }
        is.getNext(second);
        bytesRead = 2;
        w1 = first;
        w2 = second;
        uniCh = ((w1 & 0x001F) << 6) | (w2 & 0x3F);
    } else if ((first & 0xF0) == 0xE0) {
        // 3-byte sequence
        if (!is.peekNext(second))
            return 0;
        if ((second & 0xC0) != 0x80) {
            stdChar = INVALID_STD_CHAR;
            return 1;
        }
        is.getNext(second);
        bytesRead = 2;
        if (!is.peekNext(third))
            return 0;
        if ((third & 0xC0) != 0x80) {
            stdChar = INVALID_STD_CHAR;
            return 1;
        }
        is.getNext(third);
        bytesRead = 3;
        w1 = first;
        w2 = second;
        w3 = third;
        uniCh = ((w1 & 0x000F) << 12) | ((w2 & 0x003F) << 6) | (w3 & 0x003F);
    } else {
        stdChar = INVALID_STD_CHAR;
        return 1;
    }

    // translate to StdVnChar
    UKDWORD key = uniCh;
    UKDWORD *pChar = (UKDWORD *)bsearch(&key, m_vnChars, TOTAL_VNCHARS,
                                        sizeof(UKDWORD), wideCharCompare);
    if (pChar)
        stdChar = VnStdCharOffset + HIWORD(*pChar);
    else
        stdChar = uniCh;
    return 1;
}

//-------------------------------------------
int UnicodeUTF8Charset::putChar(ByteOutStream &os, StdVnChar stdChar,
                                int &outLen) {
    UnicodeChar uChar = (stdChar < VnStdCharOffset)
                            ? (UnicodeChar)stdChar
                            : m_toUnicode[stdChar - VnStdCharOffset];
    int ret;
    if (uChar < 0x0080) {
        outLen = 1;
        ret = os.putB((UKBYTE)uChar);
    } else if (uChar < 0x0800) {
        outLen = 2;
        os.putB(0xC0 | (UKBYTE)(uChar >> 6));
        ret = os.putB(0x80 | (UKBYTE)(uChar & 0x003F));
    } else {
        outLen = 3;
        os.putB(0xE0 | (UKBYTE)(uChar >> 12));
        os.putB(0x80 | (UKBYTE)((uChar >> 6) & 0x003F));
        ret = os.putB(0x80 | (UKBYTE)(uChar & 0x003F));
    }
    return ret;
}

////////////////////////////////////////
// Unicode character reference &#D;   //
////////////////////////////////////////
int hexDigitValue(unsigned char digit) {
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    return 0;
}

//--------------------------------------
int UnicodeRefCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                 int &bytesRead) {
    unsigned char ch;
    UnicodeChar uniCh;
    bytesRead = 0;
    if (!is.getNext(ch))
        return 0;
    bytesRead = 1;
    uniCh = ch;
    if (ch == '&') {
        if (is.peekNext(ch) && ch == '#') {
            is.getNext(ch);
            bytesRead++;
            if (!is.eos()) {
                is.peekNext(ch);
                if (ch != 'x' && ch != 'X') {
                    UKWORD code = 0;
                    int digits = 0;
                    while (is.peekNext(ch) && isdigit(ch) && digits < 5) {
                        is.getNext(ch);
                        bytesRead++;
                        code = code * 10 + (ch - '0');
                        digits++;
                    }
                    if (is.peekNext(ch) && ch == ';') {
                        is.getNext(ch);
                        bytesRead++;
                        uniCh = code;
                    }
                } else {
                    is.getNext(ch);
                    bytesRead++;
                    UKWORD code = 0;
                    int digits = 0;
                    while (is.peekNext(ch) && isxdigit(ch) && digits < 4) {
                        is.getNext(ch);
                        bytesRead++;
                        code = (code << 4) + hexDigitValue(ch);
                        digits++;
                    }
                    if (is.peekNext(ch) && ch == ';') {
                        is.getNext(ch);
                        bytesRead++;
                        uniCh = code;
                    }
                } // hex digits
            }
        }
    }

    // translate to StdVnChar
    UKDWORD key = uniCh;
    UKDWORD *pChar = (UKDWORD *)bsearch(&key, m_vnChars, TOTAL_VNCHARS,
                                        sizeof(UKDWORD), wideCharCompare);
    if (pChar)
        stdChar = VnStdCharOffset + HIWORD(*pChar);
    else
        stdChar = uniCh;
    return 1;
}

//--------------------------------
int UnicodeRefCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                               int &outLen) {
    UnicodeChar uChar = (stdChar < VnStdCharOffset)
                            ? (UnicodeChar)stdChar
                            : m_toUnicode[stdChar - VnStdCharOffset];
    int ret;
    if (uChar < 128) {
        outLen = 1;
        ret = os.putB((UKBYTE)uChar);
    } else {
        outLen = 2;
        os.putB((UKBYTE)'&');
        os.putB((UKBYTE)'#');

        int i, digit, prev, base;
        prev = 0;
        base = 10000;
        for (i = 0; i < 5; i++) {
            digit = uChar / base;
            if (digit || prev) {
                prev = 1;
                outLen++;
                os.putB('0' + (unsigned char)digit);
            }
            uChar %= base;
            base /= 10;
        }
        ret = os.putB((UKBYTE)';');
        outLen++;
    }
    return ret;
}

#define HEX_DIGIT(x) ((x < 10) ? ('0' + x) : ('A' + x - 10))

//--------------------------------
int UnicodeHexCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                               int &outLen) {
    UnicodeChar uChar = (stdChar < VnStdCharOffset)
                            ? (UnicodeChar)stdChar
                            : m_toUnicode[stdChar - VnStdCharOffset];
    int ret;
    if (uChar < 256) {
        outLen = 1;
        ret = os.putB((UKBYTE)uChar);
    } else {
        outLen = 3;
        os.putB('&');
        os.putB('#');
        os.putB('x');

        int i, digit;
        int prev = 0;
        int shifts = 12;

        for (i = 0; i < 4; i++) {
            digit = ((uChar >> shifts) & 0x000F);
            if (digit > 0 || prev) {
                prev = 1;
                outLen++;
                os.putB((UKBYTE)HEX_DIGIT(digit));
            }
            shifts -= 4;
        }
        ret = os.putB(';');
        outLen++;
    }
    return ret;
}

/////////////////////////////////
// Class UnicodeCStringCharset  /
/////////////////////////////////
void UnicodeCStringCharset::startInput() { m_prevIsHex = 0; }

//----------------------------------------
int UnicodeCStringCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                     int &bytesRead) {
    unsigned char ch;
    UnicodeChar uniCh;
    bytesRead = 0;
    if (!is.getNext(ch))
        return 0;
    bytesRead = 1;
    uniCh = ch;
    if (ch == '\\') {
        if (is.peekNext(ch) && (ch == 'x' || ch == 'X')) {
            is.getNext(ch);
            bytesRead++;
            UKWORD code = 0;
            int digits = 0;
            while (is.peekNext(ch) && isxdigit(ch) && digits < 4) {
                is.getNext(ch);
                bytesRead++;
                code = (code << 4) + hexDigitValue(ch);
                digits++;
            }
            uniCh = code;
        }
    }

    // translate to StdVnChar
    UKDWORD key = uniCh;
    UKDWORD *pChar = (UKDWORD *)bsearch(&key, m_vnChars, TOTAL_VNCHARS,
                                        sizeof(UKDWORD), wideCharCompare);
    if (pChar)
        stdChar = VnStdCharOffset + HIWORD(*pChar);
    else
        stdChar = uniCh;
    return 1;
}

//------------------------------------
int UnicodeCStringCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                                   int &outLen) {
    UnicodeChar uChar = (stdChar < VnStdCharOffset)
                            ? (UnicodeChar)stdChar
                            : m_toUnicode[stdChar - VnStdCharOffset];
    int ret;
    if (uChar < 128 && !isxdigit(uChar) && uChar != 'x' && uChar != 'X') {
        outLen = 1;
        ret = os.putB((UKBYTE)uChar);
    } else {
        outLen = 2;
        os.putB('\\');
        os.putB('x');

        int i, digit;
        int prev = 0;
        int shifts = 12;

        for (i = 0; i < 4; i++) {
            digit = ((uChar >> shifts) & 0x000F);
            if (digit > 0 || prev) {
                prev = 1;
                outLen++;
                os.putB((UKBYTE)HEX_DIGIT(digit));
            }
            shifts -= 4;
        }
        ret = os.isOK();
        m_prevIsHex = 1;
    }
    return ret;
}

/////////////////////////////////
// Double-byte charsets        //
/////////////////////////////////
DoubleByteCharset::DoubleByteCharset(UKWORD *vnChars) {
    m_toDoubleChar = vnChars;
    memset(m_stdMap, 0, 256 * sizeof(UKWORD));
    for (int i = 0; i < TOTAL_VNCHARS; i++) {
        if (vnChars[i] >> 8)                    // a 2-byte character
            m_stdMap[vnChars[i] >> 8] = 0xFFFF; // INVALID_STD_CHAR;
        else if (m_stdMap[vnChars[i]] == 0)
            m_stdMap[vnChars[i]] = i + 1;
        m_vnChars[i] =
            (i << 16) + vnChars[i]; // high word is used for StdChar index
    }
    qsort(m_vnChars, TOTAL_VNCHARS, sizeof(UKDWORD), wideCharCompare);
}

//---------------------------------------------
int DoubleByteCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                 int &bytesRead) {
    unsigned char ch;

    // read first byte
    bytesRead = 0;
    if (!is.getNext(ch))
        return 0;
    bytesRead = 1;
    stdChar = m_stdMap[ch];
    if (stdChar == 0)
        stdChar = ch;
    else if (stdChar == 0xFFFF)
        stdChar = INVALID_STD_CHAR;
    else {
        stdChar += VnStdCharOffset - 1;
        UKBYTE hi;
        if (is.peekNext(hi) && hi > 0) {
            // test if a double-byte character is encountered
            UKDWORD key = MAKEWORD(ch, hi);
            UKDWORD *pChar =
                (UKDWORD *)bsearch(&key, m_vnChars, TOTAL_VNCHARS,
                                   sizeof(UKDWORD), wideCharCompare);
            if (pChar) {
                stdChar = VnStdCharOffset + HIWORD(*pChar);
                bytesRead = 2;
                is.getNext(hi);
            }
        }
    }
    return 1;
}

//---------------------------------------------
int DoubleByteCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                               int &outLen) {
    int ret;
    if (stdChar >= VnStdCharOffset) {
        UKWORD wCh = m_toDoubleChar[stdChar - VnStdCharOffset];

        if (wCh & 0xFF00) {
            outLen = 2;
            os.putB((UKBYTE)(wCh & 0x00FF));
            ret = os.putB((UKBYTE)(wCh >> 8));
        } else {
            unsigned char b = (unsigned char)wCh;
            if (m_stdMap[b] == 0xFFFF)
                b = PadChar;
            outLen = 1;
            ret = os.putB(b);
        }
        /*
                outLen = 1;
                ret = os.putB((UKBYTE)(wCh & 0x00FF));
                if (wCh & 0xFF00) {
                    outLen = 2;
                    ret = os.putB((UKBYTE)(wCh >> 8));
                }
        */
    } else {
        if (stdChar > 255 || m_stdMap[stdChar]) {
            outLen = 1;
            ret = os.putB((UKBYTE)PadChar);
        } else {
            outLen = 1;
            ret = os.putB((UKBYTE)stdChar);
        }
    }
    return ret;
}

/////////////////////////////////////////////
// Class: VIQRCharset                      //
/////////////////////////////////////////////

unsigned char VIQRTones[] = {'\'', '`', '?', '~', '.'};

const char *VIQREscapes[] = {
    "://", "/", "@", "mailto:", "email:", "news:", "www", "ftp"};

const int VIQREscCount = sizeof(VIQREscapes) / sizeof(char *);

VIQRCharset::VIQRCharset(UKDWORD *vnChars) {
    memset(m_stdMap, 0, 256 * sizeof(UKWORD));
    int i;
    UKDWORD dw;
    m_vnChars = vnChars;
    for (i = 0; i < TOTAL_VNCHARS; i++) {
        dw = m_vnChars[i];
        if (!(dw & 0xffffff00)) { // single byte
            // ch = (unsigned char)(dw & 0xff);
            m_stdMap[dw] = i + 256;
        }
    }

    // set offset from base characters according to tone marks
    m_stdMap[(unsigned char)'\''] = 2;
    m_stdMap[(unsigned char)'`'] = 4;
    m_stdMap[(unsigned char)'?'] = 6;
    m_stdMap[(unsigned char)'~'] = 8;
    m_stdMap[(unsigned char)'.'] = 10;
    m_stdMap[(unsigned char)'^'] = 12;

    m_stdMap[(unsigned char)'('] = 24;
    m_stdMap[(unsigned char)'+'] = 26;
    m_stdMap[(unsigned char)'*'] = 26;
}

//---------------------------------------------------
void VIQRCharset::startInput() {
    m_suspicious = 0;
    m_atWordBeginning = 1;
    m_gotTone = 0;
    m_escAll = 0;
    if (VnCharsetLibObj.m_options.viqrEsc)
        VnCharsetLibObj.m_VIQREscPatterns.reset();
}

//---------------------------------------------------
int VIQRCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                           int &bytesRead) {
    unsigned char ch1;
    bytesRead = 0;

    if (!is.getNext(ch1))
        return 0;
    bytesRead = 1;
    stdChar = m_stdMap[ch1];

    if (VnCharsetLibObj.m_options.viqrEsc) {
        if (VnCharsetLibObj.m_VIQREscPatterns.foundAtNextChar(ch1) != -1) {
            m_escAll = 1;
        }
    }

    if (m_escAll && (ch1 == ' ' || ch1 == '\t' || ch1 == '\r' || ch1 == '\n'))
        m_escAll = 0;

    if (ch1 == '\\') {
        // ecape character , try to read next
        if (!is.getNext(ch1)) {
            bytesRead++;
            stdChar = m_stdMap[ch1];
        }
    }

    if (stdChar < 256) {
        stdChar = ch1;
    } else if (!m_escAll && !is.eos()) {
        // try to read the next byte
        unsigned char ch2;
        is.peekNext(ch2);
        unsigned char upper = toupper(ch1);
        if ((!VnCharsetLibObj.m_options.smartViqr || m_atWordBeginning) &&
            upper == 'D' && (ch2 == 'd' || ch2 == 'D')) {
            is.getNext(ch2);
            bytesRead++;
            stdChar += 2; // dd is 2 positions after d.
        } else {
            StdVnChar index = m_stdMap[ch2];

            int cond;
            if (m_suspicious) {
                cond =
                    IS_VOWEL(ch1) &&
                    (index == 2 || index == 4 ||
                     index == 8 || // not accepting ? . in suspicious mode
                     (index == 12 &&
                      (upper == 'A' || upper == 'E' || upper == 'O')) ||
                     (m_stdMap[ch2] == 24 && upper == 'A') ||
                     (m_stdMap[ch2] == 26 && (upper == 'O' || upper == 'U')));
                if (cond)
                    m_suspicious = 0;
            } else
                cond =
                    IS_VOWEL(ch1) &&
                    ((index <= 10 && index > 0 &&
                      (!m_gotTone || (index != 6 && index != 10))) ||
                     (index == 12 &&
                      (upper == 'A' || upper == 'E' || upper == 'O')) ||
                     (m_stdMap[ch2] == 24 && upper == 'A') ||
                     (m_stdMap[ch2] == 26 && (upper == 'O' || upper == 'U')));

            if (cond) {
                if (index > 0)
                    m_gotTone =
                        1; // we have a tone/breve/hook in the current word

                // ok, take this byte
                is.getNext(ch2);
                bytesRead++;
                int offset = m_stdMap[ch2];
                if (offset == 26)
                    offset = 24;
                if (offset == 24 && (ch1 == 'u' || ch1 == 'U'))
                    offset = 12;
                stdChar += offset;
                // check next byte
                if (is.peekNext(ch2)) {
                    if (index > 10 && m_stdMap[ch2] > 0 &&
                        m_stdMap[ch2] <= 10) {
                        // ok, take one more byte
                        is.getNext(ch2);
                        bytesRead++;
                        stdChar += m_stdMap[ch2];
                    }
                }
            }
        }
    }
    m_atWordBeginning = (stdChar < 256);
    if (stdChar < 256) {
        m_gotTone =
            0; // reset this flag because we are at the beginning of a new word
    }

    // adjust stdChar
    if (stdChar >= 256)
        stdChar += VnStdCharOffset - 256;
    return 1;
}

//---------------------------------------------------
void VIQRCharset::startOutput() {
    m_escapeBowl = 0;
    m_escapeRoof = 0;
    m_escapeHook = 0;
    m_escapeTone = 0;
    m_noOutEsc = 0;
    VnCharsetLibObj.m_VIQROutEscPatterns.reset();
}

//---------------------------------------------------
int VIQRCharset::putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen) {
    int ret;
    UKBYTE b;
    if (stdChar >= VnStdCharOffset) {
        outLen = 1;
        UKDWORD dw = m_vnChars[stdChar - VnStdCharOffset];

        unsigned char first = (unsigned char)dw;
        unsigned char firstUpper = toupper(first);

        b = (UKBYTE)dw;
        ret = os.putB(b);
        if (VnCharsetLibObj.m_VIQROutEscPatterns.foundAtNextChar(b) != -1)
            m_noOutEsc = 1;

        if (m_noOutEsc && (b == ' ' || b == '\t' || b == '\r' || b == '\n'))
            m_noOutEsc = 0;

        if (dw & 0x0000FF00) {
            // second byte is present
            unsigned char second = (UKBYTE)(dw >> 8);
            outLen++;
            ret = os.putB(second);

            if (dw & 0x00FF0000) {
                // third byte is present
                outLen++;
                ret = os.putB((UKBYTE)(dw >> 16));
                m_escapeTone = 0;
            } else {
                UKWORD index = m_stdMap[second];
                m_escapeTone = (index == 12 || index == 24 || index == 26);
            }

            VnCharsetLibObj.m_VIQROutEscPatterns.reset();

            m_escapeBowl = 0;
            m_escapeHook = 0;
            m_escapeRoof = 0;
        } else {
            m_escapeTone = IS_VOWEL(first);
            m_escapeBowl = (firstUpper == 'A');
            m_escapeHook = (firstUpper == 'U' || firstUpper == 'O');
            m_escapeRoof =
                (firstUpper == 'A' || firstUpper == 'E' || firstUpper == 'O');
        }
    } else {
        if (stdChar > 255) {
            outLen = 1;
            ret = os.putB((UKBYTE)PadChar);
            if (VnCharsetLibObj.m_VIQROutEscPatterns.foundAtNextChar(
                    (UKBYTE)PadChar) != -1)
                m_noOutEsc = 1;
        } else {
            outLen = 1;
            UKWORD index = m_stdMap[stdChar];
            if (!VnCharsetLibObj.m_options.viqrMixed && !m_noOutEsc &&
                (stdChar == '\\' ||
                 (index > 0 && index <= 10 && m_escapeTone) ||
                 (index == 12 && m_escapeRoof) ||
                 (index == 24 && m_escapeBowl) ||
                 (index == 26 && m_escapeHook))) {
                //(m_stdMap[stdChar] > 0 && m_stdMap[stdChar] <= 26)) {
                // tone mark, needs an escape character
                outLen++;
                ret = os.putB('\\');
                if (VnCharsetLibObj.m_VIQROutEscPatterns.foundAtNextChar(
                        '\\') != -1)
                    m_noOutEsc = 1;
            }
            b = (UKBYTE)stdChar;
            ret = os.putB(b);
            if (VnCharsetLibObj.m_VIQROutEscPatterns.foundAtNextChar(b) != -1)
                m_noOutEsc = 1;
            if (m_noOutEsc && (b == ' ' || b == '\t' || b == '\r' || b == '\n'))
                m_noOutEsc = 0;
        }
        // reset escape marks
        m_escapeBowl = 0;
        m_escapeRoof = 0;
        m_escapeHook = 0;
        m_escapeTone = 0;
    }
    return ret;
}

/////////////////////////////////////////////
// Class: UTF8VIQRCharset                  //
/////////////////////////////////////////////

//-----------------------------------------
UTF8VIQRCharset::UTF8VIQRCharset(UnicodeUTF8Charset *pUtf, VIQRCharset *pViqr) {
    m_pUtf = pUtf;
    m_pViqr = pViqr;
}

//-----------------------------------------
void UTF8VIQRCharset::startInput() {
    m_pUtf->startInput();
    m_pViqr->startInput();
}

//-----------------------------------------
void UTF8VIQRCharset::startOutput() {
    m_pUtf->startOutput();
    m_pViqr->startOutput();
}

//-----------------------------------------
int UTF8VIQRCharset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                               int &bytesRead) {
    UKBYTE ch;

    if (!is.peekNext(ch))
        return 0;

    if (ch > 0xBF && ch < 0xFE) {
        m_pViqr->startInput(); // just to reset the VIQR object state
        m_pViqr->m_suspicious = 1;
        return m_pUtf->nextInput(is, stdChar, bytesRead);
    }

    return m_pViqr->nextInput(is, stdChar, bytesRead);
}

//-----------------------------------------
int UTF8VIQRCharset::putChar(ByteOutStream &os, StdVnChar stdChar,
                             int &outLen) {
    return m_pViqr->putChar(os, stdChar, outLen);
}

//-----------------------------------------
CVnCharsetLib::CVnCharsetLib() {
    unsigned char ch;
    for (ch = 'a'; ch < 'z'; ch++)
        LoVowel[ch - 'a'] = 0;
    LoVowel['a' - 'a'] = 1;
    LoVowel['e' - 'a'] = 1;
    LoVowel['i' - 'a'] = 1;
    LoVowel['o' - 'a'] = 1;
    LoVowel['u' - 'a'] = 1;
    LoVowel['y' - 'a'] = 1;

    for (ch = 'A'; ch < 'Z'; ch++)
        HiVowel[ch - 'A'] = 0;
    HiVowel['A' - 'A'] = 1;
    HiVowel['E' - 'A'] = 1;
    HiVowel['I' - 'A'] = 1;
    HiVowel['O' - 'A'] = 1;
    HiVowel['U' - 'A'] = 1;
    HiVowel['Y' - 'A'] = 1;

    m_pUniCharset = NULL;
    m_pUniCompCharset = NULL;
    m_pUniUTF8 = NULL;
    m_pUniRef = NULL;
    m_pUniHex = NULL;
    m_pVIQRCharObj = NULL;
    m_pUVIQRCharObj = NULL;
    m_pWinCP1258 = NULL;
    m_pVnIntCharset = NULL;

    int i;
    for (i = 0; i < CONV_TOTAL_SINGLE_CHARSETS; i++)
        m_sgCharsets[i] = NULL;

    for (i = 0; i < CONV_TOTAL_DOUBLE_CHARSETS; i++)
        m_dbCharsets[i] = NULL;

    VnConvResetOptions(&m_options);
    m_VIQREscPatterns.init((char **)VIQREscapes, VIQREscCount);
    m_VIQROutEscPatterns.init((char **)VIQREscapes, VIQREscCount);
}

//-----------------------------------------
CVnCharsetLib::~CVnCharsetLib() {
    if (m_pUniCharset)
        delete m_pUniCharset;
    if (m_pUniUTF8)
        delete m_pUniUTF8;
    if (m_pUniRef)
        delete m_pUniRef;
    if (m_pUniHex)
        delete m_pUniHex;
    if (m_pVIQRCharObj)
        delete m_pVIQRCharObj;
    if (m_pUVIQRCharObj)
        delete m_pUVIQRCharObj;
    if (m_pWinCP1258)
        delete m_pWinCP1258;
    if (m_pUniCString)
        delete m_pUniCString;
    if (m_pVnIntCharset)
        delete m_pVnIntCharset;

    int i;
    for (i = 0; i < CONV_TOTAL_SINGLE_CHARSETS; i++)
        if (m_sgCharsets[i])
            delete m_sgCharsets[i];

    for (i = 0; i < CONV_TOTAL_DOUBLE_CHARSETS; i++)
        if (m_dbCharsets[i])
            delete m_dbCharsets[i];
}

//-----------------------------------------
VnCharset *CVnCharsetLib::getVnCharset(int charsetIdx) {
    switch (charsetIdx) {

    case CONV_CHARSET_UNICODE:
        if (m_pUniCharset == NULL)
            m_pUniCharset = new UnicodeCharset(UnicodeTable);
        return m_pUniCharset;
    case CONV_CHARSET_UNIDECOMPOSED:
        if (m_pUniCompCharset == NULL)
            m_pUniCompCharset =
                new UnicodeCompCharset(UnicodeTable, UnicodeComposite);
        return m_pUniCompCharset;
    case CONV_CHARSET_UNIUTF8:
    case CONV_CHARSET_XUTF8:
        if (m_pUniUTF8 == NULL)
            m_pUniUTF8 = new UnicodeUTF8Charset(UnicodeTable);
        return m_pUniUTF8;

    case CONV_CHARSET_UNIREF:
        if (m_pUniRef == NULL)
            m_pUniRef = new UnicodeRefCharset(UnicodeTable);
        return m_pUniRef;

    case CONV_CHARSET_UNIREF_HEX:
        if (m_pUniHex == NULL)
            m_pUniHex = new UnicodeHexCharset(UnicodeTable);
        return m_pUniHex;

    case CONV_CHARSET_UNI_CSTRING:
        if (m_pUniCString == NULL)
            m_pUniCString = new UnicodeCStringCharset(UnicodeTable);
        return m_pUniCString;

    case CONV_CHARSET_WINCP1258:
        if (m_pWinCP1258 == NULL)
            m_pWinCP1258 = new WinCP1258Charset(WinCP1258, WinCP1258Pre);
        return m_pWinCP1258;

    case CONV_CHARSET_VIQR:
        if (m_pVIQRCharObj == NULL)
            m_pVIQRCharObj = new VIQRCharset(VIQRTable);
        return m_pVIQRCharObj;

    case CONV_CHARSET_VNSTANDARD:
        if (m_pVnIntCharset == NULL)
            m_pVnIntCharset = new VnInternalCharset();
        return m_pVnIntCharset;

    case CONV_CHARSET_UTF8VIQR:
        if (m_pUVIQRCharObj == NULL) {
            if (m_pVIQRCharObj == NULL)
                m_pVIQRCharObj = new VIQRCharset(VIQRTable);

            if (m_pUniUTF8 == NULL)
                m_pUniUTF8 = new UnicodeUTF8Charset(UnicodeTable);
            m_pUVIQRCharObj = new UTF8VIQRCharset(m_pUniUTF8, m_pVIQRCharObj);
        }
        return m_pUVIQRCharObj;

    default:
        if (IS_SINGLE_BYTE_CHARSET(charsetIdx)) {
            int i = charsetIdx - CONV_CHARSET_TCVN3;
            if (m_sgCharsets[i] == NULL)
                m_sgCharsets[i] = new SingleByteCharset(SingleByteTables[i]);
            return m_sgCharsets[i];
        } else if (IS_DOUBLE_BYTE_CHARSET(charsetIdx)) {
            int i = charsetIdx - CONV_CHARSET_VNIWIN;
            if (m_dbCharsets[i] == NULL)
                m_dbCharsets[i] = new DoubleByteCharset(DoubleByteTables[i]);
            return m_dbCharsets[i];
        }
    }
    return NULL;
}

//-------------------------------------------------
DllExport void VnConvSetOptions(VnConvOptions *pOptions) {
    VnCharsetLibObj.m_options = *pOptions;
}

//-------------------------------------------------
DllExport void VnConvGetOptions(VnConvOptions *pOptions) {
    *pOptions = VnCharsetLibObj.m_options;
}

//-------------------------------------------------
DllExport void VnConvResetOptions(VnConvOptions *pOptions) {
    pOptions->viqrEsc = 1;
    pOptions->viqrMixed = 0;
    pOptions->toUpper = 0;
    pOptions->toLower = 0;
    pOptions->removeTone = 0;
    pOptions->smartViqr = 1;
}

/////////////////////////////////////////////
// Class WinCP1258Charset
/////////////////////////////////////////////
WinCP1258Charset::WinCP1258Charset(UKWORD *compositeChars,
                                   UKWORD *precomposedChars) {
    int i, k;
    m_toDoubleChar = compositeChars;
    memset(m_stdMap, 0, 256 * sizeof(UKWORD));

    // encode composite chars
    for (i = 0; i < TOTAL_VNCHARS; i++) {
        if (compositeChars[i] >> 8)                    // a 2-byte character
            m_stdMap[compositeChars[i] >> 8] = 0xFFFF; // INVALID_STD_CHAR;
        else if (m_stdMap[compositeChars[i]] == 0)
            m_stdMap[compositeChars[i]] = i + 1;

        m_vnChars[i] = (i << 16) +
                       compositeChars[i]; // high word is used for StdChar index
    }

    m_totalChars = TOTAL_VNCHARS;

    // add precomposed chars to the table
    for (k = 0, i = TOTAL_VNCHARS; k < TOTAL_VNCHARS; k++)
        if (precomposedChars[k] != compositeChars[k]) {
            if (precomposedChars[k] >> 8) // a 2-byte character
                m_stdMap[precomposedChars[k] >> 8] =
                    0xFFFF; // INVALID_STD_CHAR;
            else if (m_stdMap[precomposedChars[k]] == 0)
                m_stdMap[precomposedChars[k]] = k + 1;

            m_vnChars[i] = (k << 16) + precomposedChars[k];
            m_totalChars++;
            i++;
        }

    qsort(m_vnChars, m_totalChars, sizeof(UKDWORD), wideCharCompare);
}

//---------------------------------------------------------------------
// This fuction is basically the same as that of DoubleByteCharset
// with m_totalChars is used instead of constant TOTAL_VNCHARS
//---------------------------------------------------------------------
int WinCP1258Charset::nextInput(ByteInStream &is, StdVnChar &stdChar,
                                int &bytesRead) {
    unsigned char ch;

    // read first byte
    bytesRead = 0;
    if (!is.getNext(ch))
        return 0;
    bytesRead = 1;
    stdChar = m_stdMap[ch];
    if (stdChar == 0)
        stdChar = ch;
    else if (stdChar == 0xFFFF)
        stdChar = INVALID_STD_CHAR;
    else {
        stdChar += VnStdCharOffset - 1;
        UKBYTE hi;
        if (is.peekNext(hi) && hi > 0) {
            // test if a double-byte character is encountered
            UKDWORD key = MAKEWORD(ch, hi);
            UKDWORD *pChar =
                (UKDWORD *)bsearch(&key, m_vnChars, m_totalChars,
                                   sizeof(UKDWORD), wideCharCompare);
            if (pChar) {
                stdChar = VnStdCharOffset + HIWORD(*pChar);
                bytesRead = 2;
                is.getNext(hi);
            }
        }
    }
    return 1;
}

//---------------------------------------------------------------------
// This fuction is exactly the same as that of DoubleByteCharset
//---------------------------------------------------------------------
int WinCP1258Charset::putChar(ByteOutStream &os, StdVnChar stdChar,
                              int &outLen) {
    int ret;
    if (stdChar >= VnStdCharOffset) {
        UKWORD wCh = m_toDoubleChar[stdChar - VnStdCharOffset];

        if (wCh & 0xFF00) {
            outLen = 2;
            os.putB((UKBYTE)(wCh & 0x00FF));
            ret = os.putB((UKBYTE)(wCh >> 8));
        } else {
            unsigned char b = (unsigned char)wCh;
            if (m_stdMap[b] == 0xFFFF)
                b = PadChar;
            outLen = 1;
            ret = os.putB(b);
        }
    } else {
        if (stdChar > 255 || m_stdMap[stdChar]) {
            outLen = 1;
            ret = os.putB((UKBYTE)PadChar);
        } else {
            outLen = 1;
            ret = os.putB((UKBYTE)stdChar);
        }
    }
    return ret;
}

#define IS_ODD(x) (x & 1)
#define IS_EVEN(x) (!(x & 1))

StdVnChar StdVnToUpper(StdVnChar ch) {
    if (ch >= VnStdCharOffset && ch < (VnStdCharOffset + TOTAL_ALPHA_VNCHARS) &&
        IS_ODD(ch))
        ch -= 1;
    return ch;
}

//----------------------------------------
StdVnChar StdVnToLower(StdVnChar ch) {
    if (ch >= VnStdCharOffset && ch < (VnStdCharOffset + TOTAL_ALPHA_VNCHARS) &&
        IS_EVEN(ch))
        ch += 1;
    return ch;
}

//----------------------------------------
StdVnChar StdVnGetRoot(StdVnChar ch) {
    if (ch >= VnStdCharOffset && ch < VnStdCharOffset + TOTAL_VNCHARS)
        ch = VnStdCharOffset + StdVnRootChar[ch - VnStdCharOffset];
    return ch;
}
