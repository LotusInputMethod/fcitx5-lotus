/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "charset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "vnconv.h"

int vnFileStreamConvert(int inCharset, int outCharset, FILE *inf, FILE *outf);

DllExport int genConvert(VnCharset &incs, VnCharset &outcs, ByteInStream &input,
                         ByteOutStream &output) {
    StdVnChar stdChar;
    int bytesRead, bytesWritten;

    incs.startInput();
    outcs.startOutput();

    int ret = 1;
    while (!input.eos()) {
        stdChar = 0;
        if (incs.nextInput(input, stdChar, bytesRead)) {
            if (stdChar != INVALID_STD_CHAR) {
                if (VnCharsetLibObj.m_options.toLower)
                    stdChar = StdVnToLower(stdChar);
                else if (VnCharsetLibObj.m_options.toUpper)
                    stdChar = StdVnToUpper(stdChar);
                if (VnCharsetLibObj.m_options.removeTone)
                    stdChar = StdVnGetRoot(stdChar);
                ret = outcs.putChar(output, stdChar, bytesWritten);
            }
        } else
            break;
    }
    return (ret ? 0 : VNCONV_OUT_OF_MEMORY);
}

//----------------------------------------------
// Arguments:
//       inCharset: charset of input
//       outCharset: charset of output
//       input: input data
//       output: output data
//       inLen: [in]  size of input. if inLen = -1, input data is
//       null-terminated.
//              [out] if input inLen != -1, output iLen is the numbers of byte
//              left in input.
//       maxOutLen: [in] size of output.
//                  [out] number of bytes output, if enough memory
//                        number of bytes needed for output, if not enough
//                        memory
// Returns:  0 if successful
//           error code: if failed
//----------------------------------------------
// int VnConvert(int inCharset, int outCharset, UKBYTE *input, UKBYTE *output,
// int & inLen, int & maxOutLen)

DllExport int VnConvert(int inCharset, int outCharset, UKBYTE *input,
                        UKBYTE *output, int *pInLen, int *pMaxOutLen) {
    int inLen, maxOutLen;
    int ret = -1;

    inLen = *pInLen;
    maxOutLen = *pMaxOutLen;

    if (inLen != -1 && inLen < 0) // invalid inLen
        return ret;

    VnCharset *pInCharset = VnCharsetLibObj.getVnCharset(inCharset);
    VnCharset *pOutCharset = VnCharsetLibObj.getVnCharset(outCharset);

    if (!pInCharset || !pOutCharset)
        return VNCONV_INVALID_CHARSET;

    StringBIStream is(input, inLen, pInCharset->elementSize());
    StringBOStream os(output, maxOutLen);

    ret = genConvert(*pInCharset, *pOutCharset, is, os);
    *pMaxOutLen = os.getOutBytes();
    *pInLen = is.left();
    return ret;
}

//---------------------------------------
// Arguments:
//   inFile: input file name. NULL if STDIN is used
//   outFile: output file name, NULL if STDOUT is used
// Returns:
//     0: successful
//     errCode: if failed
//---------------------------------------
DllExport int VnFileConvert(int inCharset, int outCharset, const char *inFile,
                            const char *outFile) {
    FILE *inf = NULL;
    FILE *outf = NULL;
    int ret = 0;
    char tmpName[32];

    if (inFile == NULL) {
        inf = stdin;
#if defined(_WIN32)
        _setmode(_fileno(stdin), _O_BINARY);
#endif
    } else {
        inf = fopen(inFile, "rb");
        if (inf == NULL) {
            ret = VNCONV_ERR_INPUT_FILE;
            goto end;
        }
    }

    if (outFile == NULL)
        outf = stdout;
    else {
        // setup temporary output file (because real output file may be the same
        // as input file
        char outDir[256];
        strcpy(outDir, outFile);

#if defined(_WIN32)
        char *p = strrchr(outDir, '\\');
#else
        char *p = strrchr(outDir, '/');
#endif

        if (p == NULL)
            outDir[0] = 0;
        else
            *p = 0;

        strcpy(tmpName, outDir);
        strcat(tmpName, "XXXXXX");

        if (mkstemp(tmpName) == -1) {
            fclose(inf);
            ret = VNCONV_ERR_OUTPUT_FILE;
            goto end;
        }
        outf = fopen(tmpName, "wb");

        if (outf == NULL) {
            fclose(inf);
            ret = VNCONV_ERR_OUTPUT_FILE;
            goto end;
        }
    }

    ret = vnFileStreamConvert(inCharset, outCharset, inf, outf);
    if (inf != stdin)
        fclose(inf);
    if (outf != stdout) {
        fclose(outf);

        // delete output file if exisits
        if (ret == 0) {
            remove(outFile);
#if !defined(_WIN32)
            char cmd[256];
            sprintf(cmd, "mv %s %s", tmpName, outFile);
            cmd[0] = system(cmd);
#else
            if (rename(tmpName, outFile) != 0) {
                remove(tmpName);
                ret = VNCONV_ERR_OUTPUT_FILE;
                goto end;
            }
#endif
        } else
            remove(tmpName);
    }

end:
#if defined(_WIN32)
    if (inf == stdin) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
#endif
    return ret;
}

//------------------------------------------------
// Returns:
//     0: successful
//     errCode: if failed
//---------------------------------------
int vnFileStreamConvert(int inCharset, int outCharset, FILE *inf, FILE *outf) {
    VnCharset *pInCharset = VnCharsetLibObj.getVnCharset(inCharset);
    VnCharset *pOutCharset = VnCharsetLibObj.getVnCharset(outCharset);

    if (!pInCharset || !pOutCharset)
        return VNCONV_INVALID_CHARSET;

    if (outCharset == CONV_CHARSET_UNICODE) {
        UKWORD sign = 0xFEFF;
        fwrite(&sign, sizeof(UKWORD), 1, outf);
    }

    FileBIStream is;
    FileBOStream os;

    is.attach(inf);
    os.attach(outf);

    return genConvert(*pInCharset, *pOutCharset, is, os);
}

const char *ErrTable[VNCONV_LAST_ERROR] = {
    "No error",
    "Unknown error",
    "Invalid charset",
    "Error opening input file",
    "Error opening output file",
    "Error writing to output stream",
    "Not enough memory",
};

DllExport const char *VnConvErrMsg(int errCode) {
    if (errCode < 0 || errCode >= VNCONV_LAST_ERROR)
        errCode = VNCONV_UNKNOWN_ERROR;
    return ErrTable[errCode];
}
