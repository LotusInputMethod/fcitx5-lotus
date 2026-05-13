/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __CHARSET_CONVERT_H
#define __CHARSET_CONVERT_H

#if !defined(_WIN32)
#include <stdint.h>
#endif

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

#include "byteio.h"
#include "pattern.h"
#include "vnconv.h"

#define TOTAL_VNCHARS 213
#define TOTAL_ALPHA_VNCHARS 186

#if defined(_WIN32)
typedef unsigned __int32 StdVnChar;
typedef unsigned __int16 UnicodeChar;
typedef unsigned __int16 UKWORD;
typedef unsigned __int32 UKDWORD;
#else
// typedef unsigned int StdVnChar; //the size should be more specific
typedef uint32_t StdVnChar;
typedef uint16_t UnicodeChar;
typedef uint16_t UKWORD;
typedef uint32_t UKDWORD;
#endif

// typedef unsigned short UnicodeChar;
// typedef unsigned short UKWORD;

// typedef unsigned int UKDWORD; //the size should be more specific

#ifndef LOWORD
#define LOWORD(l) ((UKWORD)(l))
#endif

#ifndef HIWORD
#define HIWORD(l) ((UKWORD)(((UKDWORD)(l) >> 16) & 0xFFFF))
#endif

#ifndef MAKEWORD
#define MAKEWORD(a, b) ((UKWORD)(((UKBYTE)(a)) | ((UKWORD)((UKBYTE)(b))) << 8))
#endif

const StdVnChar VnStdCharOffset = 0x10000;
const StdVnChar INVALID_STD_CHAR = 0xFFFFFFFF;
// const unsigned char PadChar = '?'; //? is used for VIQR charset
const unsigned char PadChar = '#';
const unsigned char PadStartQuote = '\"';
const unsigned char PadEndQuote = '\"';
const unsigned char PadEllipsis = '.';

class DllInterface VnCharset {
public:
    virtual void startInput() {}
    virtual void startOutput() {}
    //	virtual UKBYTE *nextInput(UKBYTE *input, int inLen, StdVnChar & stdChar,
    // int & bytesRead) = 0;
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar,
                          int &bytesRead) = 0;

    //------------------------------------------------------------------------
    // put a character to the output after converting it
    // Arguments:
    //     output[in]: output buffer
    //     stdChar[in]: character in standard charset
    //     outLen[out]: length of converted sequence
    //     maxAvail[in]: max length available.
    // Returns: next position in output
    //------------------------------------------------------------------------
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen) = 0;
    virtual int elementSize();
    virtual ~VnCharset() {}
};

//--------------------------------------------------
class SingleByteCharset : public VnCharset {
protected:
    UKWORD m_stdMap[256];
    unsigned char *m_vnChars;

public:
    SingleByteCharset(unsigned char *vnChars);
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class VnInternalCharset : public VnCharset {
public:
    VnInternalCharset() {}
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
    virtual int elementSize();
};

//--------------------------------------------------
class UnicodeCharset : public VnCharset {
protected:
    UKDWORD m_vnChars[TOTAL_VNCHARS];
    UnicodeChar *m_toUnicode;

public:
    UnicodeCharset(UnicodeChar *vnChars);
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
    virtual int elementSize();
};

//--------------------------------------------------
class DoubleByteCharset : public VnCharset {
protected:
    UKWORD m_stdMap[256];
    UKDWORD m_vnChars[TOTAL_VNCHARS];
    UKWORD *m_toDoubleChar;

public:
    DoubleByteCharset(UKWORD *vnChars);
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class UnicodeUTF8Charset : public UnicodeCharset {
public:
    UnicodeUTF8Charset(UnicodeChar *vnChars) : UnicodeCharset(vnChars) {}

    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class UnicodeRefCharset : public UnicodeCharset {
public:
    UnicodeRefCharset(UnicodeChar *vnChars) : UnicodeCharset(vnChars) {}

    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class UnicodeHexCharset : public UnicodeRefCharset {
public:
    UnicodeHexCharset(UnicodeChar *vnChars) : UnicodeRefCharset(vnChars) {}
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class UnicodeCStringCharset : public UnicodeCharset {
protected:
    int m_prevIsHex;

public:
    UnicodeCStringCharset(UnicodeChar *vnChars) : UnicodeCharset(vnChars) {}
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
    virtual void startInput();
};

//--------------------------------------------------
class WinCP1258Charset : public VnCharset {
protected:
    UKWORD m_stdMap[256];
    UKDWORD m_vnChars[TOTAL_VNCHARS * 2];
    UKWORD *m_toDoubleChar;
    int m_totalChars;

public:
    WinCP1258Charset(UKWORD *compositeChars, UKWORD *precomposedChars);
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
struct UniCompCharInfo {
    UKDWORD compChar;
    int stdIndex;
};

class UnicodeCompCharset : public VnCharset {
protected:
    UniCompCharInfo m_info[TOTAL_VNCHARS * 2];
    UKDWORD *m_uniCompChars;
    int m_totalChars;

public:
    UnicodeCompCharset(UnicodeChar *uniChars, UKDWORD *uniCompChars);
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
    virtual int elementSize();
};

//--------------------------------------------------
class VIQRCharset : public VnCharset {
protected:
    UKDWORD *m_vnChars;
    UKWORD m_stdMap[256];
    int m_atWordBeginning;
    int m_escapeBowl;
    int m_escapeRoof;
    int m_escapeHook;
    int m_escapeTone;
    int m_gotTone;
    int m_escAll;
    int m_noOutEsc;

public:
    int m_suspicious;
    VIQRCharset(UKDWORD *vnChars);
    virtual void startInput();
    virtual void startOutput();
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class UTF8VIQRCharset : public VnCharset {

protected:
    VIQRCharset *m_pViqr;
    UnicodeUTF8Charset *m_pUtf;

public:
    UTF8VIQRCharset(UnicodeUTF8Charset *pUtf, VIQRCharset *pViqr);
    virtual void startInput();
    virtual void startOutput();
    virtual int nextInput(ByteInStream &is, StdVnChar &stdChar, int &bytesRead);
    virtual int putChar(ByteOutStream &os, StdVnChar stdChar, int &outLen);
};

//--------------------------------------------------
class DllInterface CVnCharsetLib {
protected:
    SingleByteCharset *m_sgCharsets[CONV_TOTAL_SINGLE_CHARSETS];
    DoubleByteCharset *m_dbCharsets[CONV_TOTAL_DOUBLE_CHARSETS];
    UnicodeCharset *m_pUniCharset;
    UnicodeCompCharset *m_pUniCompCharset;
    UnicodeUTF8Charset *m_pUniUTF8;
    UnicodeRefCharset *m_pUniRef;
    UnicodeHexCharset *m_pUniHex;
    VIQRCharset *m_pVIQRCharObj;
    UTF8VIQRCharset *m_pUVIQRCharObj;
    WinCP1258Charset *m_pWinCP1258;
    UnicodeCStringCharset *m_pUniCString;
    VnInternalCharset *m_pVnIntCharset;

public:
    PatternList m_VIQREscPatterns, m_VIQROutEscPatterns;
    VnConvOptions m_options;
    CVnCharsetLib();
    ~CVnCharsetLib();
    VnCharset *getVnCharset(int charsetIdx);
};

extern unsigned char SingleByteTables[][TOTAL_VNCHARS];
extern UKWORD DoubleByteTables[][TOTAL_VNCHARS];
extern UnicodeChar UnicodeTable[TOTAL_VNCHARS];
extern UKDWORD VIQRTable[TOTAL_VNCHARS];
extern UKDWORD UnicodeComposite[TOTAL_VNCHARS];
extern UKWORD WinCP1258[TOTAL_VNCHARS];
extern UKWORD WinCP1258Pre[TOTAL_VNCHARS];

extern DllInterface CVnCharsetLib VnCharsetLibObj;
extern VnConvOptions VnConvGlobalOptions;
extern int StdVnNoTone[TOTAL_VNCHARS];
extern int StdVnRootChar[TOTAL_VNCHARS];

DllInterface int genConvert(VnCharset &incs, VnCharset &outcs,
                            ByteInStream &input, ByteOutStream &output);

StdVnChar StdVnToUpper(StdVnChar ch);
StdVnChar StdVnToLower(StdVnChar ch);
StdVnChar StdVnGetRoot(StdVnChar ch);

#endif
