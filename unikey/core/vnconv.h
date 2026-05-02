/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <longp@cslab.felk.cvut.cz>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __VN_CONVERT_H
#define __VN_CONVERT_H

#if defined(_WIN32)
#if defined(UNIKEYHOOK)
#define DllInterface __declspec(dllexport)
#else
#define DllInterface __declspec(dllimport)
#endif
#define DllExport __declspec(dllexport)
#define DllImport __declspec(dllimport)
#else
#define DllInterface // not used
#define DllExport
#define DllImport
#endif

#define CONV_CHARSET_UNICODE 0
#define CONV_CHARSET_UNIUTF8 1
#define CONV_CHARSET_UNIREF 2 //&#D;
#define CONV_CHARSET_UNIREF_HEX 3
#define CONV_CHARSET_UNIDECOMPOSED 4
#define CONV_CHARSET_WINCP1258 5
#define CONV_CHARSET_UNI_CSTRING 6
#define CONV_CHARSET_VNSTANDARD 7

#define CONV_CHARSET_VIQR 10
#define CONV_CHARSET_UTF8VIQR 11
#define CONV_CHARSET_XUTF8 12

#define CONV_CHARSET_TCVN3 20
#define CONV_CHARSET_VPS 21
#define CONV_CHARSET_VISCII 22
#define CONV_CHARSET_BKHCM1 23
#define CONV_CHARSET_VIETWAREF 24
#define CONV_CHARSET_ISC 25

#define CONV_CHARSET_VNIWIN 40
#define CONV_CHARSET_BKHCM2 41
#define CONV_CHARSET_VIETWAREX 42
#define CONV_CHARSET_VNIMAC 43

#define CONV_TOTAL_SINGLE_CHARSETS 6
#define CONV_TOTAL_DOUBLE_CHARSETS 4

#define IS_SINGLE_BYTE_CHARSET(x)                                              \
    (x >= CONV_CHARSET_TCVN3 &&                                                \
     x < CONV_CHARSET_TCVN3 + CONV_TOTAL_SINGLE_CHARSETS)
#define IS_DOUBLE_BYTE_CHARSET(x)                                              \
    (x >= CONV_CHARSET_VNIWIN &&                                               \
     x < CONV_CHARSET_VNIWIN + CONV_TOTAL_DOUBLE_CHARSETS)

typedef unsigned char UKBYTE;

#if defined(__cplusplus)
extern "C" {
#endif
DllInterface int VnConvert(int inCharset, int outCharset, UKBYTE *input,
                           UKBYTE *output, int *pInLen, int *pMaxOutLen);

DllInterface int VnFileConvert(int inCharset, int outCharset,
                               const char *inFile, const char *outFile);

#if defined(__cplusplus)
}
#endif

DllInterface const char *VnConvErrMsg(int errCode);

enum VnConvError {
    VNCONV_NO_ERROR,
    VNCONV_UNKNOWN_ERROR,
    VNCONV_INVALID_CHARSET,
    VNCONV_ERR_INPUT_FILE,
    VNCONV_ERR_OUTPUT_FILE,
    VNCONV_OUT_OF_MEMORY,
    VNCONV_ERR_WRITING,
    VNCONV_LAST_ERROR
};

typedef struct _CharsetNameId CharsetNameId;

struct _CharsetNameId {
    const char *name;
    int id;
};

typedef struct _VnConvOptions VnConvOptions;

struct _VnConvOptions {
    int viqrMixed;
    int viqrEsc;
    int toUpper;
    int toLower;
    int removeTone;
    int smartViqr;
};

DllInterface void VnConvSetOptions(VnConvOptions *pOptions);
DllInterface void VnConvGetOptions(VnConvOptions *pOptions);
DllInterface void VnConvResetOptions(VnConvOptions *pOptions);

#endif
