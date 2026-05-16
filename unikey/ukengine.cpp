/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "keycons.h"
#include <iostream>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

/*
#if defined(_WIN32)
#include "keyhook.h"
#endif
*/

#include "ukengine.h"
#include "vnlexi.h"

#include "charset.h"

using namespace std;

#define ENTER_CHAR 13
#define IS_ODD(x) (x & 1)
#define IS_EVEN(x) (!(x & 1))

#define IS_STD_VN_LOWER(x)                                                     \
    ((x) >= VnStdCharOffset &&                                                 \
     (x) < (VnStdCharOffset + TOTAL_ALPHA_VNCHARS) && IS_ODD(x))
#define IS_STD_VN_UPPER(x)                                                     \
    ((x) >= VnStdCharOffset &&                                                 \
     (x) < (VnStdCharOffset + TOTAL_ALPHA_VNCHARS) && IS_EVEN(x))

bool IsVnVowel[vnl_lastChar];

extern VnLexiName AZLexiUpper[]; // defined in inputproc.cpp
extern VnLexiName AZLexiLower[];

// see vnconv/data.cpp for explanation of these characters
unsigned char SpecialWesternChars[] = {
    0x80, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    0x8B, 0x8C, 0x8E, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9E, 0x9F, 0x00};

StdVnChar IsoStdVnCharMap[256];

inline StdVnChar IsoToStdVnChar(int keyCode) {
    return (keyCode < 256) ? IsoStdVnCharMap[keyCode] : keyCode;
}

struct VowelSeqInfo {
    int len;
    int complete;
    int conSuffix; // allow consonnant suffix
    VnLexiName v[3];
    VowelSeq sub[3];

    int roofPos;
    VowelSeq withRoof;

    int hookPos;
    VowelSeq withHook; // hook & bowl
};

VowelSeqInfo VSeqList[] = {{1,
                            1,
                            1,
                            {vnl_a, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_a, vs_nil, vs_nil},
                            -1,
                            vs_ar,
                            -1,
                            vs_ab},
                           {1,
                            1,
                            1,
                            {vnl_ar, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_ar, vs_nil, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_ab},
                           {1,
                            1,
                            1,
                            {vnl_ab, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_ab, vs_nil, vs_nil},
                            -1,
                            vs_ar,
                            0,
                            vs_nil},
                           {1,
                            1,
                            1,
                            {vnl_e, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_e, vs_nil, vs_nil},
                            -1,
                            vs_er,
                            -1,
                            vs_nil},
                           {1,
                            1,
                            1,
                            {vnl_er, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_er, vs_nil, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_nil},
                           {1,
                            1,
                            1,
                            {vnl_i, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_i, vs_nil, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {1,
                            1,
                            1,
                            {vnl_o, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_o, vs_nil, vs_nil},
                            -1,
                            vs_or,
                            -1,
                            vs_oh},
                           {1,
                            1,
                            1,
                            {vnl_or, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_or, vs_nil, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_oh},
                           {1,
                            1,
                            1,
                            {vnl_oh, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_oh, vs_nil, vs_nil},
                            -1,
                            vs_or,
                            0,
                            vs_nil},
                           {1,
                            1,
                            1,
                            {vnl_u, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_u, vs_nil, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_uh},
                           {1,
                            1,
                            1,
                            {vnl_uh, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_uh, vs_nil, vs_nil},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {1,
                            1,
                            1,
                            {vnl_y, vnl_nonVnChar, vnl_nonVnChar},
                            {vs_y, vs_nil, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_a, vnl_i, vnl_nonVnChar},
                            {vs_a, vs_ai, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_a, vnl_o, vnl_nonVnChar},
                            {vs_a, vs_ao, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_a, vnl_u, vnl_nonVnChar},
                            {vs_a, vs_au, vs_nil},
                            -1,
                            vs_aru,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_a, vnl_y, vnl_nonVnChar},
                            {vs_a, vs_ay, vs_nil},
                            -1,
                            vs_ary,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_ar, vnl_u, vnl_nonVnChar},
                            {vs_ar, vs_aru, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_ar, vnl_y, vnl_nonVnChar},
                            {vs_ar, vs_ary, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_e, vnl_o, vnl_nonVnChar},
                            {vs_e, vs_eo, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            0,
                            0,
                            {vnl_e, vnl_u, vnl_nonVnChar},
                            {vs_e, vs_eu, vs_nil},
                            -1,
                            vs_eru,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_er, vnl_u, vnl_nonVnChar},
                            {vs_er, vs_eru, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_i, vnl_a, vnl_nonVnChar},
                            {vs_i, vs_ia, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            0,
                            1,
                            {vnl_i, vnl_e, vnl_nonVnChar},
                            {vs_i, vs_ie, vs_nil},
                            -1,
                            vs_ier,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            1,
                            {vnl_i, vnl_er, vnl_nonVnChar},
                            {vs_i, vs_ier, vs_nil},
                            1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_i, vnl_u, vnl_nonVnChar},
                            {vs_i, vs_iu, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            1,
                            {vnl_o, vnl_a, vnl_nonVnChar},
                            {vs_o, vs_oa, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_oab},
                           {2,
                            1,
                            1,
                            {vnl_o, vnl_ab, vnl_nonVnChar},
                            {vs_o, vs_oab, vs_nil},
                            -1,
                            vs_nil,
                            1,
                            vs_nil},
                           {2,
                            1,
                            1,
                            {vnl_o, vnl_e, vnl_nonVnChar},
                            {vs_o, vs_oe, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_o, vnl_i, vnl_nonVnChar},
                            {vs_o, vs_oi, vs_nil},
                            -1,
                            vs_ori,
                            -1,
                            vs_ohi},
                           {2,
                            1,
                            0,
                            {vnl_or, vnl_i, vnl_nonVnChar},
                            {vs_or, vs_ori, vs_nil},
                            0,
                            vs_nil,
                            -1,
                            vs_ohi},
                           {2,
                            1,
                            0,
                            {vnl_oh, vnl_i, vnl_nonVnChar},
                            {vs_oh, vs_ohi, vs_nil},
                            -1,
                            vs_ori,
                            0,
                            vs_nil},
                           {2,
                            1,
                            1,
                            {vnl_u, vnl_a, vnl_nonVnChar},
                            {vs_u, vs_ua, vs_nil},
                            -1,
                            vs_uar,
                            -1,
                            vs_uha},
                           {2,
                            1,
                            1,
                            {vnl_u, vnl_ar, vnl_nonVnChar},
                            {vs_u, vs_uar, vs_nil},
                            1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            0,
                            1,
                            {vnl_u, vnl_e, vnl_nonVnChar},
                            {vs_u, vs_ue, vs_nil},
                            -1,
                            vs_uer,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            1,
                            {vnl_u, vnl_er, vnl_nonVnChar},
                            {vs_u, vs_uer, vs_nil},
                            1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_u, vnl_i, vnl_nonVnChar},
                            {vs_u, vs_ui, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_uhi},
                           {2,
                            0,
                            1,
                            {vnl_u, vnl_o, vnl_nonVnChar},
                            {vs_u, vs_uo, vs_nil},
                            -1,
                            vs_uor,
                            -1,
                            vs_uho},
                           {2,
                            1,
                            1,
                            {vnl_u, vnl_or, vnl_nonVnChar},
                            {vs_u, vs_uor, vs_nil},
                            1,
                            vs_nil,
                            -1,
                            vs_uoh},
                           {2,
                            1,
                            1,
                            {vnl_u, vnl_oh, vnl_nonVnChar},
                            {vs_u, vs_uoh, vs_nil},
                            -1,
                            vs_uor,
                            1,
                            vs_uhoh},
                           {2,
                            0,
                            0,
                            {vnl_u, vnl_u, vnl_nonVnChar},
                            {vs_u, vs_uu, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_uhu},
                           {2,
                            1,
                            1,
                            {vnl_u, vnl_y, vnl_nonVnChar},
                            {vs_u, vs_uy, vs_nil},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_uh, vnl_a, vnl_nonVnChar},
                            {vs_uh, vs_uha, vs_nil},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_uh, vnl_i, vnl_nonVnChar},
                            {vs_uh, vs_uhi, vs_nil},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {2,
                            0,
                            1,
                            {vnl_uh, vnl_o, vnl_nonVnChar},
                            {vs_uh, vs_uho, vs_nil},
                            -1,
                            vs_nil,
                            0,
                            vs_uhoh},
                           {2,
                            1,
                            1,
                            {vnl_uh, vnl_oh, vnl_nonVnChar},
                            {vs_uh, vs_uhoh, vs_nil},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {2,
                            1,
                            0,
                            {vnl_uh, vnl_u, vnl_nonVnChar},
                            {vs_uh, vs_uhu, vs_nil},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {2,
                            0,
                            1,
                            {vnl_y, vnl_e, vnl_nonVnChar},
                            {vs_y, vs_ye, vs_nil},
                            -1,
                            vs_yer,
                            -1,
                            vs_nil},
                           {2,
                            1,
                            1,
                            {vnl_y, vnl_er, vnl_nonVnChar},
                            {vs_y, vs_yer, vs_nil},
                            1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            0,
                            0,
                            {vnl_i, vnl_e, vnl_u},
                            {vs_i, vs_ie, vs_ieu},
                            -1,
                            vs_ieru,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_i, vnl_er, vnl_u},
                            {vs_i, vs_ier, vs_ieru},
                            1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_o, vnl_a, vnl_i},
                            {vs_o, vs_oa, vs_oai},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_o, vnl_a, vnl_y},
                            {vs_o, vs_oa, vs_oay},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_o, vnl_e, vnl_o},
                            {vs_o, vs_oe, vs_oeo},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            0,
                            0,
                            {vnl_u, vnl_a, vnl_y},
                            {vs_u, vs_ua, vs_uay},
                            -1,
                            vs_uary,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_u, vnl_ar, vnl_y},
                            {vs_u, vs_uar, vs_uary},
                            1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            0,
                            0,
                            {vnl_u, vnl_o, vnl_i},
                            {vs_u, vs_uo, vs_uoi},
                            -1,
                            vs_uori,
                            -1,
                            vs_uhoi},
                           {3,
                            0,
                            0,
                            {vnl_u, vnl_o, vnl_u},
                            {vs_u, vs_uo, vs_uou},
                            -1,
                            vs_nil,
                            -1,
                            vs_uhou},
                           {3,
                            1,
                            0,
                            {vnl_u, vnl_or, vnl_i},
                            {vs_u, vs_uor, vs_uori},
                            1,
                            vs_nil,
                            -1,
                            vs_uohi},
                           {3,
                            0,
                            0,
                            {vnl_u, vnl_oh, vnl_i},
                            {vs_u, vs_uoh, vs_uohi},
                            -1,
                            vs_uori,
                            1,
                            vs_uhohi},
                           {3,
                            0,
                            0,
                            {vnl_u, vnl_oh, vnl_u},
                            {vs_u, vs_uoh, vs_uohu},
                            -1,
                            vs_nil,
                            1,
                            vs_uhohu},
                           {3,
                            1,
                            0,
                            {vnl_u, vnl_y, vnl_a},
                            {vs_u, vs_uy, vs_uya},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            0,
                            1,
                            {vnl_u, vnl_y, vnl_e},
                            {vs_u, vs_uy, vs_uye},
                            -1,
                            vs_uyer,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            1,
                            {vnl_u, vnl_y, vnl_er},
                            {vs_u, vs_uy, vs_uyer},
                            2,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_u, vnl_y, vnl_u},
                            {vs_u, vs_uy, vs_uyu},
                            -1,
                            vs_nil,
                            -1,
                            vs_nil},
                           {3,
                            0,
                            0,
                            {vnl_uh, vnl_o, vnl_i},
                            {vs_uh, vs_uho, vs_uhoi},
                            -1,
                            vs_nil,
                            0,
                            vs_uhohi},
                           {3,
                            0,
                            0,
                            {vnl_uh, vnl_o, vnl_u},
                            {vs_uh, vs_uho, vs_uhou},
                            -1,
                            vs_nil,
                            0,
                            vs_uhohu},
                           {3,
                            1,
                            0,
                            {vnl_uh, vnl_oh, vnl_i},
                            {vs_uh, vs_uhoh, vs_uhohi},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_uh, vnl_oh, vnl_u},
                            {vs_uh, vs_uhoh, vs_uhohu},
                            -1,
                            vs_nil,
                            0,
                            vs_nil},
                           {3,
                            0,
                            0,
                            {vnl_y, vnl_e, vnl_u},
                            {vs_y, vs_ye, vs_yeu},
                            -1,
                            vs_yeru,
                            -1,
                            vs_nil},
                           {3,
                            1,
                            0,
                            {vnl_y, vnl_er, vnl_u},
                            {vs_y, vs_yer, vs_yeru},
                            1,
                            vs_nil,
                            -1,
                            vs_nil}};

struct ConSeqInfo {
    int len;
    VnLexiName c[3];
    bool suffix;
};

ConSeqInfo CSeqList[] = {{1, {vnl_b, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_c, vnl_nonVnChar, vnl_nonVnChar}, true},
                         {2, {vnl_c, vnl_h, vnl_nonVnChar}, true},
                         {1, {vnl_d, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_dd, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {2, {vnl_d, vnl_z, vnl_nonVnChar}, false},
                         {1, {vnl_g, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {2, {vnl_g, vnl_h, vnl_nonVnChar}, false},
                         {2, {vnl_g, vnl_i, vnl_nonVnChar}, false},
                         {3, {vnl_g, vnl_i, vnl_n}, false},
                         {1, {vnl_h, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_k, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {2, {vnl_k, vnl_h, vnl_nonVnChar}, false},
                         {1, {vnl_l, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_m, vnl_nonVnChar, vnl_nonVnChar}, true},
                         {1, {vnl_n, vnl_nonVnChar, vnl_nonVnChar}, true},
                         {2, {vnl_n, vnl_g, vnl_nonVnChar}, true},
                         {3, {vnl_n, vnl_g, vnl_h}, false},
                         {2, {vnl_n, vnl_h, vnl_nonVnChar}, true},
                         {1, {vnl_p, vnl_nonVnChar, vnl_nonVnChar}, true},
                         {2, {vnl_p, vnl_h, vnl_nonVnChar}, false},
                         {1, {vnl_q, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {2, {vnl_q, vnl_u, vnl_nonVnChar}, false},
                         {1, {vnl_r, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_s, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_t, vnl_nonVnChar, vnl_nonVnChar}, true},
                         {2, {vnl_t, vnl_h, vnl_nonVnChar}, false},
                         {2, {vnl_t, vnl_r, vnl_nonVnChar}, false},
                         {1, {vnl_v, vnl_nonVnChar, vnl_nonVnChar}, false},
                         {1, {vnl_x, vnl_nonVnChar, vnl_nonVnChar}, false}};

const int VSeqCount = sizeof(VSeqList) / sizeof(VowelSeqInfo);
struct VSeqPair {
    VnLexiName v[3];
    VowelSeq vs;
};
VSeqPair SortedVSeqList[VSeqCount];

const int CSeqCount = sizeof(CSeqList) / sizeof(ConSeqInfo);
struct CSeqPair {
    VnLexiName c[3];
    ConSeq cs;
};
CSeqPair SortedCSeqList[CSeqCount];

struct VCPair {
    VowelSeq v;
    ConSeq c;
};

VCPair VCPairList[] = {{vs_a, cs_c},    {vs_a, cs_ch},   {vs_a, cs_m},
                       {vs_a, cs_n},    {vs_a, cs_ng},   {vs_a, cs_nh},
                       {vs_a, cs_p},    {vs_a, cs_t},    {vs_ar, cs_c},
                       {vs_ar, cs_m},   {vs_ar, cs_n},   {vs_ar, cs_ng},
                       {vs_ar, cs_p},   {vs_ar, cs_t},   {vs_ab, cs_c},
                       {vs_ab, cs_m},   {vs_ab, cs_n},   {vs_ab, cs_ng},
                       {vs_ab, cs_p},   {vs_ab, cs_t},

                       {vs_e, cs_c},    {vs_e, cs_ch},   {vs_e, cs_m},
                       {vs_e, cs_n},    {vs_e, cs_ng},   {vs_e, cs_nh},
                       {vs_e, cs_p},    {vs_e, cs_t},    {vs_er, cs_c},
                       {vs_er, cs_ch},  {vs_er, cs_m},   {vs_er, cs_n},
                       {vs_er, cs_nh},  {vs_er, cs_p},   {vs_er, cs_t},

                       {vs_i, cs_c},    {vs_i, cs_ch},   {vs_i, cs_m},
                       {vs_i, cs_n},    {vs_i, cs_nh},   {vs_i, cs_p},
                       {vs_i, cs_t},

                       {vs_o, cs_c},    {vs_o, cs_m},    {vs_o, cs_n},
                       {vs_o, cs_ng},   {vs_o, cs_p},    {vs_o, cs_t},
                       {vs_or, cs_c},   {vs_or, cs_m},   {vs_or, cs_n},
                       {vs_or, cs_ng},  {vs_or, cs_p},   {vs_or, cs_t},
                       {vs_oh, cs_m},   {vs_oh, cs_n},   {vs_oh, cs_p},
                       {vs_oh, cs_t},

                       {vs_u, cs_c},    {vs_u, cs_m},    {vs_u, cs_n},
                       {vs_u, cs_ng},   {vs_u, cs_p},    {vs_u, cs_t},
                       {vs_uh, cs_c},   {vs_uh, cs_m},   {vs_uh, cs_n},
                       {vs_uh, cs_ng},  {vs_uh, cs_t},

                       {vs_y, cs_t},    {vs_ie, cs_c},   {vs_ie, cs_m},
                       {vs_ie, cs_n},   {vs_ie, cs_ng},  {vs_ie, cs_p},
                       {vs_ie, cs_t},   {vs_ier, cs_c},  {vs_ier, cs_m},
                       {vs_ier, cs_n},  {vs_ier, cs_ng}, {vs_ier, cs_p},
                       {vs_ier, cs_t},

                       {vs_oa, cs_c},   {vs_oa, cs_ch},  {vs_oa, cs_m},
                       {vs_oa, cs_n},   {vs_oa, cs_ng},  {vs_oa, cs_nh},
                       {vs_oa, cs_p},   {vs_oa, cs_t},   {vs_oab, cs_c},
                       {vs_oab, cs_m},  {vs_oab, cs_n},  {vs_oab, cs_ng},
                       {vs_oab, cs_t},

                       {vs_oe, cs_n},   {vs_oe, cs_t},

                       {vs_ua, cs_n},   {vs_ua, cs_ng},  {vs_ua, cs_t},
                       {vs_uar, cs_n},  {vs_uar, cs_ng}, {vs_uar, cs_t},

                       {vs_ue, cs_c},   {vs_ue, cs_ch},  {vs_ue, cs_n},
                       {vs_ue, cs_nh},  {vs_uer, cs_c},  {vs_uer, cs_ch},
                       {vs_uer, cs_n},  {vs_uer, cs_nh},

                       {vs_uo, cs_c},   {vs_uo, cs_m},   {vs_uo, cs_n},
                       {vs_uo, cs_ng},  {vs_uo, cs_p},   {vs_uo, cs_t},
                       {vs_uor, cs_c},  {vs_uor, cs_m},  {vs_uor, cs_n},
                       {vs_uor, cs_ng}, {vs_uor, cs_t},  {vs_uho, cs_c},
                       {vs_uho, cs_m},  {vs_uho, cs_n},  {vs_uho, cs_ng},
                       {vs_uho, cs_p},  {vs_uho, cs_t},  {vs_uhoh, cs_c},
                       {vs_uhoh, cs_m}, {vs_uhoh, cs_n}, {vs_uhoh, cs_ng},
                       {vs_uhoh, cs_p}, {vs_uhoh, cs_t},

                       {vs_uy, cs_c},   {vs_uy, cs_ch},  {vs_uy, cs_n},
                       {vs_uy, cs_nh},  {vs_uy, cs_p},   {vs_uy, cs_t},

                       {vs_ye, cs_m},   {vs_ye, cs_n},   {vs_ye, cs_ng},
                       {vs_ye, cs_p},   {vs_ye, cs_t},   {vs_yer, cs_m},
                       {vs_yer, cs_n},  {vs_yer, cs_ng}, {vs_yer, cs_t},

                       {vs_uye, cs_n},  {vs_uye, cs_t},  {vs_uyer, cs_n},
                       {vs_uyer, cs_t}

};

const int VCPairCount = sizeof(VCPairList) / sizeof(VCPair);

// TODO: auto-complete: e.g. luan -> lua^n

typedef int (UkEngine::*UkKeyProc)(UkKeyEvent &ev);

UkKeyProc UkKeyProcList[vneCount] = {
    &UkEngine::processRoof,    // vneRoofAll
    &UkEngine::processRoof,    // vneRoof_a
    &UkEngine::processRoof,    // vneRoof_e
    &UkEngine::processRoof,    // vneRoof_o
    &UkEngine::processHook,    // vneHookAll
    &UkEngine::processHook,    // vneHook_uo
    &UkEngine::processHook,    // vneHook_u
    &UkEngine::processHook,    // vneHook_o
    &UkEngine::processHook,    // vneBowl
    &UkEngine::processDd,      // vneDd
    &UkEngine::processTone,    // vneTone0
    &UkEngine::processTone,    // vneTone1
    &UkEngine::processTone,    // vneTone2
    &UkEngine::processTone,    // vneTone3
    &UkEngine::processTone,    // vneTone4
    &UkEngine::processTone,    // vneTone5
    &UkEngine::processTelexW,  // vne_telex_w
    &UkEngine::processMapChar, // vneMapChar
    &UkEngine::processEscChar, // vneEscChar
    &UkEngine::processAppend   // vneNormal
};

VowelSeq lookupVSeq(VnLexiName v1, VnLexiName v2 = vnl_nonVnChar,
                    VnLexiName v3 = vnl_nonVnChar);
ConSeq lookupCSeq(VnLexiName c1, VnLexiName c2 = vnl_nonVnChar,
                  VnLexiName c3 = vnl_nonVnChar);

bool UkEngine::m_classInit = false;

//------------------------------------------------
int tripleVowelCompare(const void *p1, const void *p2) {
    VSeqPair *t1 = (VSeqPair *)p1;
    VSeqPair *t2 = (VSeqPair *)p2;

    for (int i = 0; i < 3; i++) {
        if (t1->v[i] < t2->v[i])
            return -1;
        if (t1->v[i] > t2->v[i])
            return 1;
    }
    return 0;
}

//------------------------------------------------
int tripleConCompare(const void *p1, const void *p2) {
    CSeqPair *t1 = (CSeqPair *)p1;
    CSeqPair *t2 = (CSeqPair *)p2;

    for (int i = 0; i < 3; i++) {
        if (t1->c[i] < t2->c[i])
            return -1;
        if (t1->c[i] > t2->c[i])
            return 1;
    }
    return 0;
}

//------------------------------------------------
int VCPairCompare(const void *p1, const void *p2) {
    VCPair *t1 = (VCPair *)p1;
    VCPair *t2 = (VCPair *)p2;

    if (t1->v < t2->v)
        return -1;
    if (t1->v > t2->v)
        return 1;

    if (t1->c < t2->c)
        return -1;
    if (t1->c > t2->c)
        return 1;
    return 0;
}

//----------------------------------------------------------
bool isValidCV(ConSeq c, VowelSeq v) {
    if (c == cs_nil || v == vs_nil)
        return true;

    VowelSeqInfo &vInfo = VSeqList[v];

    // gi doesn't go with i
    // qu doesn't go with u, uh
    // q  doesn't go with any vowel
    if ((c == cs_gi && vInfo.v[0] == vnl_i) ||
        (c == cs_qu && (vInfo.v[0] == vnl_u || vInfo.v[0] == vnl_uh)) ||
        (c == cs_q))
        return false;

    // k can only go with the following vowel sequences
    if (c == cs_k) {
        static VowelSeq kVseq[] = {vs_e,   vs_i,    vs_y,  vs_er, vs_eo,
                                   vs_eu,  vs_eru,  vs_ia, vs_ie, vs_ier,
                                   vs_ieu, vs_ieru, vs_nil};
        int i;
        for (i = 0; kVseq[i] != vs_nil && kVseq[i] != v; i++)
            ;
        return (kVseq[i] != vs_nil);
    }

    // More checks
    return true;
}

//----------------------------------------------------------
bool isValidVC(VowelSeq v, ConSeq c) {
    if (v == vs_nil || c == cs_nil)
        return true;

    VowelSeqInfo &vInfo = VSeqList[v];
    if (!vInfo.conSuffix)
        return false;

    ConSeqInfo &cInfo = CSeqList[c];
    if (!cInfo.suffix)
        return false;

    VCPair p;
    p.v = v;
    p.c = c;
    if (bsearch(&p, VCPairList, VCPairCount, sizeof(VCPair), VCPairCompare))
        return true;

    return false;
}

//----------------------------------------------------------
bool isValidCVC(ConSeq c1, VowelSeq v, ConSeq c2) {
    if (v == vs_nil)
        return (c1 == cs_nil || c2 != cs_nil);

    if (c1 == cs_nil)
        return isValidVC(v, c2);

    if (c2 == cs_nil)
        return isValidCV(c1, v);

    bool okCV = isValidCV(c1, v);
    bool okVC = isValidVC(v, c2);

    if (okCV && okVC)
        return true;

    if (!okVC) {
        // check some exceptions: vc fails but cvc passes

        // quyn, quynh
        if (c1 == cs_qu && v == vs_y && (c2 == cs_n || c2 == cs_nh))
            return true;

        // gieng, gie^ng
        if (c1 == cs_gi && (v == vs_e || v == vs_er) &&
            (c2 == cs_n || c2 == cs_ng))
            return true;
    }
    return false;
}

//------------------------------------------------
void engineClassInit() {
    int i, j;

    for (i = 0; i < VSeqCount; i++) {
        for (j = 0; j < 3; j++)
            SortedVSeqList[i].v[j] = VSeqList[i].v[j];
        SortedVSeqList[i].vs = (VowelSeq)i;
    }

    for (i = 0; i < CSeqCount; i++) {
        for (j = 0; j < 3; j++)
            SortedCSeqList[i].c[j] = CSeqList[i].c[j];
        SortedCSeqList[i].cs = (ConSeq)i;
    }

    qsort(SortedVSeqList, VSeqCount, sizeof(VSeqPair), tripleVowelCompare);
    qsort(SortedCSeqList, CSeqCount, sizeof(CSeqPair), tripleConCompare);
    qsort(VCPairList, VCPairCount, sizeof(VCPair), VCPairCompare);

    for (i = 0; i < vnl_lastChar; i++)
        IsVnVowel[i] = true;

    unsigned char ch;
    for (ch = 'a'; ch <= 'z'; ch++) {
        if (ch != 'a' && ch != 'e' && ch != 'i' && ch != 'o' && ch != 'u' &&
            ch != 'y') {
            IsVnVowel[AZLexiLower[ch - 'a']] = false;
            IsVnVowel[AZLexiUpper[ch - 'a']] = false;
        }
    }
    IsVnVowel[vnl_dd] = false;
    IsVnVowel[vnl_DD] = false;
}

//------------------------------------------------
VowelSeq lookupVSeq(VnLexiName v1, VnLexiName v2, VnLexiName v3) {
    VSeqPair key;
    key.v[0] = v1;
    key.v[1] = v2;
    key.v[2] = v3;

    VSeqPair *pInfo = (VSeqPair *)bsearch(&key, SortedVSeqList, VSeqCount,
                                          sizeof(VSeqPair), tripleVowelCompare);
    if (pInfo == 0)
        return vs_nil;
    return pInfo->vs;
}

//------------------------------------------------
ConSeq lookupCSeq(VnLexiName c1, VnLexiName c2, VnLexiName c3) {
    CSeqPair key;
    key.c[0] = c1;
    key.c[1] = c2;
    key.c[2] = c3;

    CSeqPair *pInfo = (CSeqPair *)bsearch(&key, SortedCSeqList, CSeqCount,
                                          sizeof(CSeqPair), tripleConCompare);
    if (pInfo == 0)
        return cs_nil;
    return pInfo->cs;
}

//------------------------------------------------------------------
int UkEngine::processRoof(UkKeyEvent &ev) {
    if (!m_pCtrl->vietKey || m_current < 0 || m_buffer[m_current].vOffset < 0)
        return processAppend(ev);

    VnLexiName target;
    switch (ev.evType) {
    case vneRoof_a:
        target = vnl_ar;
        break;
    case vneRoof_e:
        target = vnl_er;
        break;
    case vneRoof_o:
        target = vnl_or;
        break;
    default:
        target = vnl_nonVnChar;
    }

    VowelSeq vs, newVs;
    int i, vStart, vEnd;
    int curTonePos, newTonePos, tone;
    int changePos;
    bool roofRemoved = false;

    vEnd = m_current - m_buffer[m_current].vOffset;
    vs = m_buffer[vEnd].vseq;
    vStart = vEnd - (VSeqList[vs].len - 1);
    curTonePos = vStart + getTonePosition(vs, vEnd == m_current);
    tone = m_buffer[curTonePos].tone;

    bool doubleChangeUO = false;
    if (vs == vs_uho || vs == vs_uhoh || vs == vs_uhoi || vs == vs_uhohi) {
        // special cases: u+o+ -> uo^, u+o -> uo^, u+o+i -> uo^i, u+oi -> uo^i
        newVs = lookupVSeq(vnl_u, vnl_or, VSeqList[vs].v[2]);
        doubleChangeUO = true;
    } else {
        newVs = VSeqList[vs].withRoof;
    }

    VowelSeqInfo *pInfo;

    if (newVs == vs_nil) {
        if (VSeqList[vs].roofPos == -1)
            return processAppend(ev); // roof is not applicable

        // a roof already exists -> undo roof
        VnLexiName curCh = m_buffer[vStart + VSeqList[vs].roofPos].vnSym;
        if (target != vnl_nonVnChar && curCh != target)
            return processAppend(
                ev); // specific roof and the roof character don't match

        VnLexiName newCh =
            (curCh == vnl_ar) ? vnl_a : ((curCh == vnl_er) ? vnl_e : vnl_o);
        changePos = vStart + VSeqList[vs].roofPos;

        if (!m_pCtrl->options.freeMarking && changePos != m_current)
            return processAppend(ev);

        markChange(changePos);
        m_buffer[changePos].vnSym = newCh;

        if (VSeqList[vs].len == 3)
            newVs =
                lookupVSeq(m_buffer[vStart].vnSym, m_buffer[vStart + 1].vnSym,
                           m_buffer[vStart + 2].vnSym);
        else if (VSeqList[vs].len == 2)
            newVs =
                lookupVSeq(m_buffer[vStart].vnSym, m_buffer[vStart + 1].vnSym);
        else
            newVs = lookupVSeq(m_buffer[vStart].vnSym);

        pInfo = &VSeqList[newVs];
        roofRemoved = true;
    } else {
        pInfo = &VSeqList[newVs];
        if (target != vnl_nonVnChar && pInfo->v[pInfo->roofPos] != target)
            return processAppend(ev);

        // check validity of new VC and CV
        bool valid = true;
        ConSeq c1 = cs_nil;
        ConSeq c2 = cs_nil;
        if (m_buffer[m_current].c1Offset != -1)
            c1 = m_buffer[m_current - m_buffer[m_current].c1Offset].cseq;

        if (m_buffer[m_current].c2Offset != -1)
            c2 = m_buffer[m_current - m_buffer[m_current].c2Offset].cseq;

        valid = isValidCVC(c1, newVs, c2);
        if (!valid)
            return processAppend(ev);

        if (doubleChangeUO) {
            changePos = vStart;
        } else {
            changePos = vStart + pInfo->roofPos;
        }
        if (!m_pCtrl->options.freeMarking && changePos != m_current)
            return processAppend(ev);
        markChange(changePos);
        if (doubleChangeUO) {
            m_buffer[vStart].vnSym = vnl_u;
            m_buffer[vStart + 1].vnSym = vnl_or;
        } else {
            m_buffer[changePos].vnSym = pInfo->v[pInfo->roofPos];
        }
    }

    for (i = 0; i < pInfo->len; i++) { // update sub-sequences
        m_buffer[vStart + i].vseq = pInfo->sub[i];
    }

    // check if tone re-position is needed
    newTonePos = vStart + getTonePosition(newVs, vEnd == m_current);
    /* //For now, users don't seem to like the following processing, thus
    commented out if (roofRemoved && tone != 0 &&
        (!pInfo->complete || changePos == curTonePos)) {
        //remove tone if the vowel sequence becomes incomplete as a result of
    roof removal OR
        //if removed roof is at the same position as the current tone
        markChange(curTonePos);
        m_buffer[curTonePos].tone = 0;
    } else
    */
    if (curTonePos != newTonePos && tone != 0) {
        markChange(newTonePos);
        m_buffer[newTonePos].tone = tone;
        markChange(curTonePos);
        m_buffer[curTonePos].tone = 0;
    }

    if (roofRemoved) {
        m_singleMode = false;
        processAppend(ev);
        m_reverted = true;
    }

    return 1;
}

//------------------------------------------------------------------
// can only be called from processHook
//------------------------------------------------------------------
int UkEngine::processHookWithUO(UkKeyEvent &ev) {
    VowelSeq vs, newVs;
    int i, vStart, vEnd;
    int curTonePos, newTonePos, tone;
    bool hookRemoved = false;
    bool removeWithUndo = true;
    bool toneRemoved = false;

    (void)toneRemoved; // fix warning

    VnLexiName *v;

    if (!m_pCtrl->options.freeMarking && m_buffer[m_current].vOffset != 0)
        return processAppend(ev);

    vEnd = m_current - m_buffer[m_current].vOffset;
    vs = m_buffer[vEnd].vseq;
    vStart = vEnd - (VSeqList[vs].len - 1);
    v = VSeqList[vs].v;
    curTonePos = vStart + getTonePosition(vs, vEnd == m_current);
    tone = m_buffer[curTonePos].tone;

    switch (ev.evType) {
    case vneHook_u:
        if (v[0] == vnl_u) {
            newVs = VSeqList[vs].withHook;
            markChange(vStart);
            m_buffer[vStart].vnSym = vnl_uh;
        } else { // v[0] = vnl_uh, -> uo
            newVs = lookupVSeq(vnl_u, vnl_o, v[2]);
            markChange(vStart);
            m_buffer[vStart].vnSym = vnl_u;
            m_buffer[vStart + 1].vnSym = vnl_o;
            hookRemoved = true;
            toneRemoved = (m_buffer[vStart].tone != 0);
        }
        break;
    case vneHook_o:
        if (v[1] == vnl_o || v[1] == vnl_or) {
            if (vEnd == m_current && VSeqList[vs].len == 2 &&
                m_buffer[m_current].form == vnw_cv &&
                m_buffer[m_current - 2].cseq == cs_th) {
                // o|o^ -> o+
                newVs = VSeqList[vs].withHook;
                markChange(vStart + 1);
                m_buffer[vStart + 1].vnSym = vnl_oh;
            } else {
                newVs = lookupVSeq(vnl_uh, vnl_oh, v[2]);
                if (v[0] == vnl_u) {
                    markChange(vStart);
                    m_buffer[vStart].vnSym = vnl_uh;
                    m_buffer[vStart + 1].vnSym = vnl_oh;
                } else {
                    markChange(vStart + 1);
                    m_buffer[vStart + 1].vnSym = vnl_oh;
                }
            }
        } else { // v[1] = vnl_oh, -> uo
            newVs = lookupVSeq(vnl_u, vnl_o, v[2]);
            if (v[0] == vnl_uh) {
                markChange(vStart);
                m_buffer[vStart].vnSym = vnl_u;
                m_buffer[vStart + 1].vnSym = vnl_o;
            } else {
                markChange(vStart + 1);
                m_buffer[vStart + 1].vnSym = vnl_o;
            }
            hookRemoved = true;
            toneRemoved = (m_buffer[vStart + 1].tone != 0);
        }
        break;
    default: // vneHookAll, vneHookUO:
        if (v[0] == vnl_u) {
            if (v[1] == vnl_o || v[1] == vnl_or) {
                // uo -> uo+ if prefixed by "h", "kh", "th", or stand alone
                if ((vs == vs_uo || vs == vs_uor) && vEnd == m_current &&
                    ((m_buffer[m_current].form == vnw_cv &&
                      (m_buffer[m_current - 2].cseq == cs_h ||
                       m_buffer[m_current - 2].cseq == cs_kh ||
                       m_buffer[m_current - 2].cseq == cs_th)) ||
                     m_buffer[m_current].form == vnw_v)) {
                    newVs = vs_uoh;
                    markChange(vStart + 1);
                    m_buffer[vStart + 1].vnSym = vnl_oh;
                } else {
                    // uo -> u+o+
                    newVs = VSeqList[vs].withHook;
                    markChange(vStart);
                    m_buffer[vStart].vnSym = vnl_uh;
                    newVs = VSeqList[newVs].withHook;
                    m_buffer[vStart + 1].vnSym = vnl_oh;
                }
            } else { // uo+ -> u+o+
                newVs = VSeqList[vs].withHook;
                markChange(vStart);
                m_buffer[vStart].vnSym = vnl_uh;
            }
        } else {                 // v[0] == vnl_uh
            if (v[1] == vnl_o) { // u+o -> u+o+
                newVs = VSeqList[vs].withHook;
                markChange(vStart + 1);
                m_buffer[vStart + 1].vnSym = vnl_oh;
            } else { // v[1] == vnl_oh, u+o+ -> uo
                newVs = lookupVSeq(vnl_u, vnl_o, v[2]); // vs_uo;
                markChange(vStart);
                m_buffer[vStart].vnSym = vnl_u;
                m_buffer[vStart + 1].vnSym = vnl_o;
                hookRemoved = true;
                toneRemoved = (m_buffer[vStart].tone != 0 ||
                               m_buffer[vStart + 1].tone != 0);
            }
        }
        break;
    }

    VowelSeqInfo *p = &VSeqList[newVs];
    for (i = 0; i < p->len; i++) { // update sub-sequences
        m_buffer[vStart + i].vseq = p->sub[i];
    }

    // check if tone re-position is needed
    newTonePos = vStart + getTonePosition(newVs, vEnd == m_current);
    /* //For now, users don't seem to like the following processing, thus
    commented out if (hookRemoved && tone != 0 && (!p->complete || toneRemoved))
    {
        //remove tone if the vowel sequence becomes incomplete as a result of
    hook removal
        //OR if a removed hook is at the same position as the current tone
        markChange(curTonePos);
        m_buffer[curTonePos].tone = 0;
    }
    else
    */
    if (curTonePos != newTonePos && tone != 0) {
        markChange(newTonePos);
        m_buffer[newTonePos].tone = tone;
        markChange(curTonePos);
        m_buffer[curTonePos].tone = 0;
    }

    if (hookRemoved && removeWithUndo) {
        m_singleMode = false;
        processAppend(ev);
        m_reverted = true;
    }

    return 1;
}

//------------------------------------------------------------------
int UkEngine::processHook(UkKeyEvent &ev) {
    if (!m_pCtrl->vietKey || m_current < 0 || m_buffer[m_current].vOffset < 0)
        return processAppend(ev);

    VowelSeq vs, newVs;
    int i, vStart, vEnd;
    int curTonePos, newTonePos, tone;
    int changePos;
    bool hookRemoved = false;
    VowelSeqInfo *pInfo;
    VnLexiName *v;

    vEnd = m_current - m_buffer[m_current].vOffset;
    vs = m_buffer[vEnd].vseq;

    v = VSeqList[vs].v;

    if (VSeqList[vs].len > 1 && ev.evType != vneBowl &&
        (v[0] == vnl_u || v[0] == vnl_uh) &&
        (v[1] == vnl_o || v[1] == vnl_oh || v[1] == vnl_or))
        return processHookWithUO(ev);

    vStart = vEnd - (VSeqList[vs].len - 1);
    curTonePos = vStart + getTonePosition(vs, vEnd == m_current);
    tone = m_buffer[curTonePos].tone;

    newVs = VSeqList[vs].withHook;
    if (newVs == vs_nil) {
        if (VSeqList[vs].hookPos == -1)
            return processAppend(ev); // hook is not applicable

        // a hook already exists -> undo hook
        VnLexiName curCh = m_buffer[vStart + VSeqList[vs].hookPos].vnSym;
        VnLexiName newCh =
            (curCh == vnl_ab) ? vnl_a : ((curCh == vnl_uh) ? vnl_u : vnl_o);
        changePos = vStart + VSeqList[vs].hookPos;
        if (!m_pCtrl->options.freeMarking && changePos != m_current)
            return processAppend(ev);

        switch (ev.evType) {
        case vneHook_u:
            if (curCh != vnl_uh)
                return processAppend(ev);
            break;
        case vneHook_o:
            if (curCh != vnl_oh)
                return processAppend(ev);
            break;
        case vneBowl:
            if (curCh != vnl_ab)
                return processAppend(ev);
            break;
        default:
            if (ev.evType == vneHook_uo && curCh == vnl_ab)
                return processAppend(ev);
        }

        markChange(changePos);
        m_buffer[changePos].vnSym = newCh;

        if (VSeqList[vs].len == 3)
            newVs =
                lookupVSeq(m_buffer[vStart].vnSym, m_buffer[vStart + 1].vnSym,
                           m_buffer[vStart + 2].vnSym);
        else if (VSeqList[vs].len == 2)
            newVs =
                lookupVSeq(m_buffer[vStart].vnSym, m_buffer[vStart + 1].vnSym);
        else
            newVs = lookupVSeq(m_buffer[vStart].vnSym);

        pInfo = &VSeqList[newVs];
        hookRemoved = true;
    } else {
        pInfo = &VSeqList[newVs];

        switch (ev.evType) {
        case vneHook_u:
            if (pInfo->v[pInfo->hookPos] != vnl_uh)
                return processAppend(ev);
            break;
        case vneHook_o:
            if (pInfo->v[pInfo->hookPos] != vnl_oh)
                return processAppend(ev);
            break;
        case vneBowl:
            if (pInfo->v[pInfo->hookPos] != vnl_ab)
                return processAppend(ev);
            break;
        default: // vneHook_uo, vneHookAll
            if (ev.evType == vneHook_uo && pInfo->v[pInfo->hookPos] == vnl_ab)
                return processAppend(ev);
        }

        // check validity of new VC and CV
        bool valid = true;
        ConSeq c1 = cs_nil;
        ConSeq c2 = cs_nil;
        if (m_buffer[m_current].c1Offset != -1)
            c1 = m_buffer[m_current - m_buffer[m_current].c1Offset].cseq;

        if (m_buffer[m_current].c2Offset != -1)
            c2 = m_buffer[m_current - m_buffer[m_current].c2Offset].cseq;

        valid = isValidCVC(c1, newVs, c2);

        if (!valid)
            return processAppend(ev);

        changePos = vStart + pInfo->hookPos;
        if (!m_pCtrl->options.freeMarking && changePos != m_current)
            return processAppend(ev);

        markChange(changePos);
        m_buffer[changePos].vnSym = pInfo->v[pInfo->hookPos];
    }

    for (i = 0; i < pInfo->len; i++) { // update sub-sequences
        m_buffer[vStart + i].vseq = pInfo->sub[i];
    }

    // check if tone re-position is needed
    newTonePos = vStart + getTonePosition(newVs, vEnd == m_current);
    /* //For now, users don't seem to like the following processing, thus
    commented out if (hookRemoved && tone != 0 &&
        (!pInfo->complete || (hookRemoved && curTonePos == changePos))) {
        //remove tone if the vowel sequence becomes incomplete as a result of
    hook removal
        //OR if a removed hook was at the same position as the current tone
        markChange(curTonePos);
        m_buffer[curTonePos].tone = 0;
    }
    else */
    if (curTonePos != newTonePos && tone != 0) {
        markChange(newTonePos);
        m_buffer[newTonePos].tone = tone;
        markChange(curTonePos);
        m_buffer[curTonePos].tone = 0;
    }

    if (hookRemoved) {
        m_singleMode = false;
        processAppend(ev);
        m_reverted = true;
    }

    return 1;
}

//----------------------------------------------------------
int UkEngine::getTonePosition(VowelSeq vs, bool terminated) const {
    VowelSeqInfo &info = VSeqList[vs];
    if (info.len == 1)
        return 0;

    if (info.roofPos != -1)
        return info.roofPos;
    if (info.hookPos != -1) {
        if (vs == vs_uhoh || vs == vs_uhohi ||
            vs == vs_uhohu) // u+o+, u+o+u, u+o+i
            return 1;
        return info.hookPos;
    }

    if (info.len == 3)
        return 1;

    if (m_pCtrl->options.modernStyle &&
        (vs == vs_oa || vs == vs_oe || vs == vs_uy))
        return 1;

    return terminated ? 0 : 1;
}

//----------------------------------------------------------
int UkEngine::processTone(UkKeyEvent &ev) {
    if (m_current < 0 || !m_pCtrl->vietKey)
        return processAppend(ev);

    if (m_buffer[m_current].form == vnw_c &&
        (m_buffer[m_current].cseq == cs_gi ||
         m_buffer[m_current].cseq == cs_gin)) {
        int p = (m_buffer[m_current].cseq == cs_gi) ? m_current : m_current - 1;
        if (m_buffer[p].tone == 0 && ev.tone == 0)
            return processAppend(ev);
        markChange(p);
        if (m_buffer[p].tone == ev.tone) {
            m_buffer[p].tone = 0;
            m_singleMode = false;
            processAppend(ev);
            m_reverted = true;
            return 1;
        }
        m_buffer[p].tone = ev.tone;
        return 1;
    }

    if (m_buffer[m_current].vOffset < 0)
        return processAppend(ev);

    int vEnd;
    VowelSeq vs;

    vEnd = m_current - m_buffer[m_current].vOffset;
    vs = m_buffer[vEnd].vseq;
    VowelSeqInfo &info = VSeqList[vs];
    if (m_pCtrl->options.spellCheckEnabled && !m_pCtrl->options.freeMarking &&
        !info.complete)
        return processAppend(ev);

    if (m_buffer[m_current].form == vnw_vc ||
        m_buffer[m_current].form == vnw_cvc) {
        ConSeq cs = m_buffer[m_current].cseq;
        if ((cs == cs_c || cs == cs_ch || cs == cs_p || cs == cs_t) &&
            (ev.tone == 2 || ev.tone == 3 || ev.tone == 4))
            return processAppend(ev); // c, ch, p, t suffixes don't allow ` ? ~
    }

    int toneOffset = getTonePosition(vs, vEnd == m_current);
    int tonePos = vEnd - (info.len - 1) + toneOffset;

    if (m_buffer[tonePos].tone == 0 && ev.tone == 0)
        return processAppend(ev);

    if (m_buffer[tonePos].tone == ev.tone) {
        markChange(tonePos);
        m_buffer[tonePos].tone = 0;
        m_singleMode = false;
        processAppend(ev);
        m_reverted = true;
        return 1;
    }

    markChange(tonePos);
    m_buffer[tonePos].tone = ev.tone;
    return 1;
}

//----------------------------------------------------------
int UkEngine::processDd(UkKeyEvent &ev) {
    if (!m_pCtrl->vietKey || m_current < 0)
        return processAppend(ev);

    int pos;

    // we want to allow dd even in non-vn sequence, because dd is used a lot in
    // abbreviation we allow dd only if preceding character is not a vowel
    if (m_buffer[m_current].form == vnw_nonVn &&
        m_buffer[m_current].vnSym == vnl_d &&
        (m_buffer[m_current - 1].vnSym == vnl_nonVnChar ||
         !IsVnVowel[m_buffer[m_current - 1].vnSym])) {
        m_singleMode = true;
        pos = m_current;
        markChange(pos);
        m_buffer[pos].cseq = cs_dd;
        m_buffer[pos].vnSym = vnl_dd;
        m_buffer[pos].form = vnw_c;
        m_buffer[pos].c1Offset = 0;
        m_buffer[pos].c2Offset = -1;
        m_buffer[pos].vOffset = -1;
        return 1;
    }

    if (m_buffer[m_current].c1Offset < 0) {
        return processAppend(ev);
    }

    pos = m_current - m_buffer[m_current].c1Offset;
    if (!m_pCtrl->options.freeMarking && pos != m_current)
        return processAppend(ev);

    if (m_buffer[pos].cseq == cs_d) {
        markChange(pos);
        m_buffer[pos].cseq = cs_dd;
        m_buffer[pos].vnSym = vnl_dd;
        // never spellcheck a word which starts with dd, because it's used alot
        // in abbreviation
        m_singleMode = true;
        return 1;
    }

    if (m_buffer[pos].cseq == cs_dd) {
        // undo dd
        markChange(pos);
        m_buffer[pos].cseq = cs_d;
        m_buffer[pos].vnSym = vnl_d;
        m_singleMode = false;
        processAppend(ev);
        m_reverted = true;
        return 1;
    }

    return processAppend(ev);
}

//----------------------------------------------------------
VnLexiName changeCase(VnLexiName x) {
    if (x == vnl_nonVnChar)
        return x;
    if (!(x & 0x01))
        return (VnLexiName)(x + 1);
    return (VnLexiName)(x - 1);
}

//----------------------------------------------------------
inline VnLexiName vnToLower(VnLexiName x) {
    if (x == vnl_nonVnChar)
        return x;
    if (!(x & 0x01)) // even
        return (VnLexiName)(x + 1);
    return x;
}

//----------------------------------------------------------
int UkEngine::processMapChar(UkKeyEvent &ev) {
    int capsLockOn = 0;
    int shiftPressed = 0;
    if (m_keyCheckFunc)
        m_keyCheckFunc(&shiftPressed, &capsLockOn);

    if (capsLockOn)
        ev.vnSym = changeCase(ev.vnSym);

    int ret = processAppend(ev);
    if (!m_pCtrl->vietKey)
        return ret;

    if (m_current >= 0 && m_buffer[m_current].form != vnw_empty &&
        m_buffer[m_current].form != vnw_nonVn) {
        return 1;
    }

    if (m_current < 0)
        return 0;

    // mapChar doesn't apply
    m_current--;
    WordInfo &entry = m_buffer[m_current];

    bool undo = false;
    // test if undo is needed
    if (entry.form != vnw_empty && entry.form != vnw_nonVn) {
        VnLexiName prevSym = entry.vnSym;
        if (entry.caps) {
            prevSym = (VnLexiName)(prevSym - 1);
        }
        if (prevSym == ev.vnSym) {
            if (entry.form != vnw_c) {
                int vStart, vEnd, curTonePos, newTonePos, tone;
                VowelSeq vs, newVs;

                vEnd = m_current - entry.vOffset;
                vs = m_buffer[vEnd].vseq;
                vStart = vEnd - VSeqList[vs].len + 1;
                curTonePos = vStart + getTonePosition(vs, vEnd == m_current);
                tone = m_buffer[curTonePos].tone;
                markChange(m_current);
                m_current--;

                // check if tone position is needed
                if (tone != 0 && m_current >= 0 &&
                    (m_buffer[m_current].form == vnw_v ||
                     m_buffer[m_current].form == vnw_cv)) {
                    newVs = m_buffer[m_current].vseq;
                    newTonePos = vStart + getTonePosition(newVs, true);
                    if (newTonePos != curTonePos) {
                        markChange(newTonePos);
                        m_buffer[newTonePos].tone = tone;
                        markChange(curTonePos);
                        m_buffer[curTonePos].tone = 0;
                    }
                }
            } else {
                markChange(m_current);
                m_current--;
            }
            undo = true;
        }
    }

    ev.evType = vneNormal;
    ev.chType = m_pCtrl->input.getCharType(ev.keyCode);
    ev.vnSym = IsoToVnLexi(ev.keyCode);
    ret = processAppend(ev);
    if (undo) {
        m_singleMode = false;
        m_reverted = true;
        return 1;
    }
    return ret;
}

//----------------------------------------------------------
int UkEngine::processTelexW(UkKeyEvent &ev) {
    if (!m_pCtrl->vietKey)
        return processAppend(ev);

    int ret;
    static bool usedAsMapChar = false;
    int capsLockOn = 0;
    int shiftPressed = 0;
    if (m_keyCheckFunc)
        m_keyCheckFunc(&shiftPressed, &capsLockOn);

    if (usedAsMapChar) {
        ev.evType = vneMapChar;
        ev.vnSym = isupper(ev.keyCode) ? vnl_Uh : vnl_uh;
        if (capsLockOn)
            ev.vnSym = changeCase(ev.vnSym);
        ev.chType = ukcVn;
        ret = processMapChar(ev);
        if (ret == 0) {
            if (m_current >= 0)
                m_current--;
            usedAsMapChar = false;
            ev.evType = vneHookAll;
            return processHook(ev);
        }
        return ret;
    }

    ev.evType = vneHookAll;
    usedAsMapChar = false;
    ret = processHook(ev);
    if (ret == 0) {
        if (m_current >= 0)
            m_current--;
        ev.evType = vneMapChar;
        ev.vnSym = isupper(ev.keyCode) ? vnl_Uh : vnl_uh;
        if (capsLockOn)
            ev.vnSym = changeCase(ev.vnSym);
        ev.chType = ukcVn;
        usedAsMapChar = true;
        return processMapChar(ev);
    }
    return ret;
}

//----------------------------------------------------------
int UkEngine::checkEscapeVIQR(UkKeyEvent &ev) {
    if (m_current < 0)
        return 0;
    WordInfo &entry = m_buffer[m_current];
    int escape = 0;
    if (entry.form == vnw_v || entry.form == vnw_cv) {
        switch (ev.keyCode) {
        case '^':
            escape = (entry.vnSym == vnl_a || entry.vnSym == vnl_o ||
                      entry.vnSym == vnl_e);
            break;
        case '(':
            escape = (entry.vnSym == vnl_a);
            break;
        case '+':
            escape = (entry.vnSym == vnl_o || entry.vnSym == vnl_u);
            break;
        case '\'':
        case '`':
        case '?':
        case '~':
        case '.':
            escape = (entry.tone == 0);
            break;
        }
    } else if (entry.form == vnw_nonVn) {
        unsigned char ch = toupper(entry.keyCode);
        switch (ev.keyCode) {
        case '^':
            escape = (ch == 'A' || ch == 'O' || ch == 'E');
            break;
        case '(':
            escape = (ch == 'A');
            break;
        case '+':
            escape = (ch == 'O' || ch == 'U');
            break;
        case '\'':
        case '`':
        case '?':
        case '~':
        case '.':
            escape = (ch == 'A' || ch == 'E' || ch == 'I' || ch == 'O' ||
                      ch == 'U' || ch == 'Y');
            break;
        }
    }

    if (escape) {
        m_current++;
        WordInfo *p = &m_buffer[m_current];
        p->form = (ev.chType == ukcWordBreak) ? vnw_empty : vnw_nonVn;
        p->c1Offset = p->c2Offset = p->vOffset = -1;
        p->keyCode = '?';
        p->vnSym = vnl_nonVnChar;

        m_current++;
        p++;
        p->form = (ev.chType == ukcWordBreak) ? vnw_empty : vnw_nonVn;
        p->c1Offset = p->c2Offset = p->vOffset = -1;
        p->keyCode = ev.keyCode;
        p->vnSym = vnl_nonVnChar;

        // write output
        m_pOutBuf[0] = '\\';
        m_pOutBuf[1] = ev.keyCode;
        *m_pOutSize = 2;
        m_outputWritten = true;
    }
    return escape;
}

//----------------------------------------------------------
int UkEngine::processAppend(UkKeyEvent &ev) {
    int ret = 0;
    switch (ev.chType) {
    case ukcReset:
#if defined(_WIN32)
        if (ev.keyCode == ENTER_CHAR) {
            if (m_pCtrl->options.macroEnabled && macroMatch(ev))
                return 1;
        }
#endif
        reset();
        return 0;
    case ukcWordBreak:
        m_singleMode = false;
        return processWordEnd(ev);
    case ukcNonVn: {
        if (m_pCtrl->vietKey && m_pCtrl->charsetId == CONV_CHARSET_VIQR &&
            checkEscapeVIQR(ev))
            return 1;

        m_current++;
        WordInfo &entry = m_buffer[m_current];
        entry.form = (ev.chType == ukcWordBreak) ? vnw_empty : vnw_nonVn;
        entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
        entry.keyCode = ev.keyCode;
        entry.vnSym = vnToLower(ev.vnSym);
        entry.tone = 0;
        entry.caps = (entry.vnSym != ev.vnSym);
        if (!m_pCtrl->vietKey || m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
            return 0;
        markChange(m_current);
        return 1;
    }
    case ukcVn: {
        if (IsVnVowel[ev.vnSym]) {
            VnLexiName v = (VnLexiName)StdVnNoTone[vnToLower(ev.vnSym)];
            if (m_current >= 0 && m_buffer[m_current].form == vnw_c &&
                ((m_buffer[m_current].cseq == cs_q && v == vnl_u) ||
                 (m_buffer[m_current].cseq == cs_g && v == vnl_i))) {
                return appendConsonnant(
                    ev); // process u after q, i after g as consonnants
            }
            return appendVowel(ev);
        }
        return appendConsonnant(ev);
    } break;
    }

    return ret;
}

//----------------------------------------------------------
int UkEngine::appendVowel(UkKeyEvent &ev) {
    bool autoCompleted = false;
    bool complexEvent = false;

    m_current++;
    WordInfo &entry = m_buffer[m_current];

    VnLexiName lowerSym = vnToLower(ev.vnSym);
    VnLexiName canSym = (VnLexiName)StdVnNoTone[lowerSym];

    entry.vnSym = canSym;
    entry.caps = (lowerSym != ev.vnSym);
    entry.tone = (lowerSym - canSym) / 2;
    entry.keyCode = ev.keyCode;

    if (m_current == 0 || !m_pCtrl->vietKey) {
        entry.form = vnw_v;
        entry.c1Offset = entry.c2Offset = -1;
        entry.vOffset = 0;
        entry.vseq = lookupVSeq(canSym);

        if (!m_pCtrl->vietKey ||
            ((m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING) &&
             isalpha(entry.keyCode))) {
            return 0;
        }
        markChange(m_current);
        return 1;
    }

    WordInfo &prev = m_buffer[m_current - 1];
    VowelSeq vs, newVs;
    ConSeq cs;
    int prevTonePos;
    int tone, newTone, tonePos, newTonePos;

    switch (prev.form) {

    case vnw_empty:
        entry.form = vnw_v;
        entry.c1Offset = entry.c2Offset = -1;
        entry.vOffset = 0;
        entry.vseq = newVs = lookupVSeq(canSym);
        break;

    case vnw_nonVn:
    case vnw_cvc:
    case vnw_vc:
        entry.form = vnw_nonVn;
        entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
        break;

    case vnw_v:
    case vnw_cv:
        vs = prev.vseq;

        prevTonePos = (m_current - 1) - (VSeqList[vs].len - 1) +
                      getTonePosition(vs, true);
        tone = m_buffer[prevTonePos].tone;

        // u+o/uo+ + u/i -> u+o+ + u/i
        if ((vs == vs_uoh || vs == vs_uho) &&
            (lowerSym == vnl_i || lowerSym == vnl_u)) {
            if (vs == vs_uho) {
                markChange(m_current - 1);
                prev.vnSym = vnl_oh;
                prev.vseq = vs_uhoh;
            } else {
                markChange(m_current - 2);
                m_buffer[m_current - 2].vnSym = vnl_uh;
                m_buffer[m_current - 2].vseq = vs_uh;
            }

            vs = vs_uhoh;
            complexEvent = true;
        }

        if (lowerSym != canSym && tone != 0) // new sym has a tone, but there's
                                             // is already a preceeding tone
            newVs = vs_nil;
        else {
            if (VSeqList[vs].len == 3)
                newVs = vs_nil;
            else if (VSeqList[vs].len == 2)
                newVs =
                    lookupVSeq(VSeqList[vs].v[0], VSeqList[vs].v[1], canSym);
            else
                newVs = lookupVSeq(VSeqList[vs].v[0], canSym);
        }

        if (newVs != vs_nil && prev.form == vnw_cv) {
            cs = m_buffer[m_current - 1 - prev.c1Offset].cseq;
            if (!isValidCV(cs, newVs))
                newVs = vs_nil;
        }

        if (newVs == vs_nil) {
            entry.form = vnw_nonVn;
            entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
            break;
        }

        entry.form = prev.form;
        if (prev.form == vnw_cv)
            entry.c1Offset = prev.c1Offset + 1;
        else
            entry.c1Offset = -1;
        entry.c2Offset = -1;
        entry.vOffset = 0;
        entry.vseq = newVs;
        entry.tone = 0;

        newTone = (lowerSym - canSym) / 2;
        if (tone == 0) {
            if (newTone != 0) {
                tone = newTone;
                tonePos = getTonePosition(newVs, true) +
                          ((m_current - 1) - VSeqList[vs].len + 1);
                markChange(tonePos);
                m_buffer[tonePos].tone = tone;
                return 1;
            }
        } else {
            newTonePos = getTonePosition(newVs, true) +
                         ((m_current - 1) - VSeqList[vs].len + 1);
            if (newTonePos != prevTonePos) {
                markChange(prevTonePos);
                m_buffer[prevTonePos].tone = 0;
                markChange(newTonePos);
                if (newTone != 0)
                    tone = newTone;
                m_buffer[newTonePos].tone = tone;
                return 1;
            }
            if (newTone != 0 && newTone != tone) {
                tone = newTone;
                markChange(prevTonePos);
                m_buffer[prevTonePos].tone = tone;
                return 1;
            }
        }

        break;
    case vnw_c:
        newVs = lookupVSeq(canSym);
        cs = prev.cseq;
        if (!isValidCV(cs, newVs)) {
            entry.form = vnw_nonVn;
            entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
            break;
        }

        entry.form = vnw_cv;
        entry.c1Offset = 1;
        entry.c2Offset = -1;
        entry.vOffset = 0;
        entry.vseq = newVs;

        if (cs == cs_gi && prev.tone != 0) {
            if (entry.tone == 0)
                entry.tone = prev.tone;
            markChange(m_current - 1);
            prev.tone = 0;
            return 1;
        }

        break;
    }

    if (complexEvent) {
        return 1;
    }

    if (!autoCompleted && (m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING) &&
        isalpha(entry.keyCode)) {
        return 0;
    }

    markChange(m_current);
    return 1;
}

//----------------------------------------------------------
int UkEngine::appendConsonnant(UkKeyEvent &ev) {
    bool complexEvent = false;
    m_current++;
    WordInfo &entry = m_buffer[m_current];

    VnLexiName lowerSym = vnToLower(ev.vnSym);

    entry.vnSym = lowerSym;
    entry.caps = (lowerSym != ev.vnSym);
    entry.keyCode = ev.keyCode;
    entry.tone = 0;

    if (m_current == 0 || !m_pCtrl->vietKey) {
        entry.form = vnw_c;
        entry.c1Offset = 0;
        entry.c2Offset = -1;
        entry.vOffset = -1;
        entry.cseq = lookupCSeq(lowerSym);
        if (!m_pCtrl->vietKey || m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
            return 0;
        markChange(m_current);
        return 1;
    }

    ConSeq cs, newCs, c1;
    VowelSeq vs, newVs;
    bool isValid;

    WordInfo &prev = m_buffer[m_current - 1];

    switch (prev.form) {
    case vnw_nonVn:
        entry.form = vnw_nonVn;
        entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
        if (m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
            return 0;
        markChange(m_current);
        return 1;
    case vnw_empty:
        entry.form = vnw_c;
        entry.c1Offset = 0;
        entry.c2Offset = -1;
        entry.vOffset = -1;
        entry.cseq = lookupCSeq(lowerSym);
        if (m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
            return 0;
        markChange(m_current);
        return 1;
    case vnw_v:
    case vnw_cv:
        vs = prev.vseq;
        newVs = vs;
        if (vs == vs_uoh || vs == vs_uho) {
            newVs = vs_uhoh;
        }

        c1 = cs_nil;
        if (prev.c1Offset != -1)
            c1 = m_buffer[m_current - 1 - prev.c1Offset].cseq;

        newCs = lookupCSeq(lowerSym);
        isValid = isValidCVC(c1, newVs, newCs);

        if (isValid) {
            // check u+o -> u+o+
            if (vs == vs_uho) {
                markChange(m_current - 1);
                prev.vnSym = vnl_oh;
                prev.vseq = vs_uhoh;
                complexEvent = true;
            } else if (vs == vs_uoh) {
                markChange(m_current - 2);
                m_buffer[m_current - 2].vnSym = vnl_uh;
                m_buffer[m_current - 2].vseq = vs_uh;
                prev.vseq = vs_uhoh;
                complexEvent = true;
            }

            if (prev.form == vnw_v) {
                entry.form = vnw_vc;
                entry.c1Offset = -1;
                entry.c2Offset = 0;
                entry.vOffset = 1;
            } else { // prev == vnw_cv
                entry.form = vnw_cvc;
                entry.c1Offset = prev.c1Offset + 1;
                entry.c2Offset = 0;
                entry.vOffset = 1;
            }
            entry.cseq = newCs;

            // reposition tone if needed
            int oldIdx = (m_current - 1) - (VSeqList[vs].len - 1) +
                         getTonePosition(vs, true);
            if (m_buffer[oldIdx].tone != 0) {
                int newIdx = (m_current - 1) - (VSeqList[newVs].len - 1) +
                             getTonePosition(newVs, false);
                if (newIdx != oldIdx) {
                    markChange(newIdx);
                    m_buffer[newIdx].tone = m_buffer[oldIdx].tone;
                    markChange(oldIdx);
                    m_buffer[oldIdx].tone = 0;
                    return 1;
                }
            }
        } else {
            entry.form = vnw_nonVn;
            entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
        }

        if (complexEvent) {
            return 1;
        }

        if (m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
            return 0;
        markChange(m_current);
        return 1;
    case vnw_c:
    case vnw_vc:
    case vnw_cvc:
        cs = prev.cseq;
        if (CSeqList[cs].len == 3)
            newCs = cs_nil;
        else if (CSeqList[cs].len == 2)
            newCs = lookupCSeq(CSeqList[cs].c[0], CSeqList[cs].c[1], lowerSym);
        else
            newCs = lookupCSeq(CSeqList[cs].c[0], lowerSym);

        if (newCs != cs_nil && (prev.form == vnw_vc || prev.form == vnw_cvc)) {
            // Check CVC combination
            c1 = cs_nil;
            if (prev.c1Offset != -1)
                c1 = m_buffer[m_current - 1 - prev.c1Offset].cseq;

            int vIdx = (m_current - 1) - prev.vOffset;
            vs = m_buffer[vIdx].vseq;
            isValid = isValidCVC(c1, vs, newCs);

            if (!isValid)
                newCs = cs_nil;
        }

        if (newCs == cs_nil) {
            entry.form = vnw_nonVn;
            entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
        } else {
            if (prev.form == vnw_c) {
                entry.form = vnw_c;
                entry.c1Offset = 0;
                entry.c2Offset = -1;
                entry.vOffset = -1;
            } else if (prev.form == vnw_vc) {
                entry.form = vnw_vc;
                entry.c1Offset = -1;
                entry.c2Offset = 0;
                entry.vOffset = prev.vOffset + 1;
            } else { // vnw_cvc
                entry.form = vnw_cvc;
                entry.c1Offset = prev.c1Offset + 1;
                entry.c2Offset = 0;
                entry.vOffset = prev.vOffset + 1;
            }
            entry.cseq = newCs;
        }
        if (m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
            return 0;
        markChange(m_current);
        return 1;
    }

    if (m_pCtrl->charsetId != CONV_CHARSET_UNI_CSTRING)
        return 0;
    markChange(m_current);
    return 1;
}

//----------------------------------------------------------
int UkEngine::processEscChar(UkKeyEvent &ev) {
    if (m_pCtrl->vietKey && m_current >= 0 &&
        m_buffer[m_current].form != vnw_empty &&
        m_buffer[m_current].form != vnw_nonVn) {
        m_toEscape = true;
    }
    return processAppend(ev);
}

//----------------------------------------------------------
void UkEngine::pass(int keyCode) {
    UkKeyEvent ev;
    m_pCtrl->input.keyCodeToEvent(keyCode, ev);
    processAppend(ev);
}

//---------------------------------------------
// This can be called only after other processing have been done.
// The new event is supposed to be put into m_buffer already
//---------------------------------------------
int UkEngine::processNoSpellCheck(UkKeyEvent &ev) {
    WordInfo &entry = m_buffer[m_current];
    if (IsVnVowel[entry.vnSym]) {
        entry.form = vnw_v;
        entry.vOffset = 0;
        entry.vseq = lookupVSeq(entry.vnSym);
        entry.c1Offset = entry.c2Offset = -1;
    } else {
        entry.form = vnw_c;
        entry.c1Offset = 0;
        entry.c2Offset = -1;
        entry.vOffset = -1;
        entry.cseq = lookupCSeq(entry.vnSym);
    }

    if (ev.evType == vneNormal &&
        ((entry.keyCode >= 'a' && entry.keyCode <= 'z') ||
         (entry.keyCode >= 'A' && entry.keyCode <= 'Z')))
        return 0;
    markChange(m_current);
    return 1;
}
//----------------------------------------------------------
int UkEngine::process(unsigned int keyCode, int &backs, unsigned char *outBuf,
                      int &outSize, UkOutputType &outType) {
    UkKeyEvent ev;
    prepareBuffer();
    m_backs = 0;
    m_changePos = m_current + 1;
    m_pOutBuf = outBuf;
    m_pOutSize = &outSize;
    m_outputWritten = false;
    m_reverted = false;
    m_keyRestored = false;
    m_keyRestoring = false;
    m_outType = UkCharOutput;

    m_pCtrl->input.keyCodeToEvent(keyCode, ev);

    int ret;
    if (!m_toEscape) {
        ret = (this->*UkKeyProcList[ev.evType])(ev);
    } else {
        m_toEscape = false;
        if (m_current < 0 || ev.evType == vneNormal ||
            ev.evType == vneEscChar) {
            ret = processAppend(ev);
        } else {
            m_current--;
            processAppend(ev);
            markChange(m_current); // this will assign m_backs to 1 and mark the
                                   // character for output
            ret = 1;
        }
    }

    if (m_pCtrl->vietKey && m_current >= 0 &&
        m_buffer[m_current].form == vnw_nonVn && ev.chType == ukcVn &&
        (!m_pCtrl->options.spellCheckEnabled || m_singleMode)) {

        // The spell check has failed, but because we are in non-spellcheck
        // mode, we consider the new character as the beginning of a new word
        ret = processNoSpellCheck(ev);
        /*
        if ((!m_pCtrl->options.spellCheckEnabled || m_singleMode) ||
            ( !m_reverted &&
              (m_current < 1 || m_buffer[m_current-1].form != vnw_nonVn)) ) {

            ret = processNoSpellCheck(ev);
        }
        */
    }

    // we add key to key buffer only if that key has not caused a reset
    if (m_current >= 0) {
        ev.chType = m_pCtrl->input.getCharType(ev.keyCode);
        m_keyCurrent++;
        m_keyStrokes[m_keyCurrent].ev = ev;
        m_keyStrokes[m_keyCurrent].converted = (ret && !m_keyRestored);
    }

    if (ret == 0) {
        backs = 0;
        outSize = 0;
        outType = m_outType;
        return 0;
    }

    backs = m_backs;
    if (!m_outputWritten) {
        writeOutput(outBuf, outSize);
    }
    outType = m_outType;

    return ret;
}
//----------------------------------------------------------
void UkEngine::rebuildChar(VnLexiName ch, int &backs, unsigned char *outBuf,
                           int &outSize) {
    static const std::unordered_map<VnLexiName, UkKeyEvName> map{
        {vnl_Ar, vneRoof_a}, {vnl_Ab, vneBowl},   {vnl_DD, vneDd},
        {vnl_Er, vneRoof_e}, {vnl_Or, vneRoof_o}, {vnl_Oh, vneHook_o},
        {vnl_Uh, vneHook_u}};

    if (ch == vnl_nonVnChar) {
        return;
    }

    prepareBuffer();
    m_backs = 0;
    m_changePos = m_current + 1;
    m_pOutBuf = outBuf;
    m_pOutSize = &outSize;

    UkKeyEvent ev;

    auto rootChar = StdVnRootChar[ch];
    auto noToneChar = StdVnNoTone[ch];

    auto keyCode = UnicodeTable[rootChar];
    m_pCtrl->input.keyCodeToEvent(keyCode, ev);

    // root char
    processAppend(ev);

    // add root char to key strokes
    m_keyCurrent++;
    m_keyStrokes[m_keyCurrent].ev = ev;
    m_keyStrokes[m_keyCurrent].converted = true;

    // modify vowel
    auto it =
        map.find(noToneChar % 2 == 0 ? static_cast<VnLexiName>(noToneChar)
                                     : static_cast<VnLexiName>(noToneChar - 1));
    if (it != map.end()) {
        ev.evType = it->second;
        (this->*UkKeyProcList[ev.evType])(ev);
    }

    // tone
    auto tone = (ch - noToneChar) / 2;
    if (tone >= 1 && tone <= 5) {
        ev.evType = vneTone0 + tone;
        ev.tone = tone;
        (this->*UkKeyProcList[ev.evType])(ev);
    }

    backs = m_backs;
    writeOutput(outBuf, outSize);
}

//----------------------------------------------------------
// Returns 0 on success
//         error code otherwise
//  outBuf: buffer to write
//  outSize: [in] size of buffer in bytes
//           [out] bytes written to buffer
//----------------------------------------------------------
int UkEngine::writeOutput(unsigned char *outBuf, int &outSize) {
    StdVnChar stdChar;
    int i, bytesWritten;
    int ret = 1;
    StringBOStream os(outBuf, outSize);
    VnCharset *pCharset = VnCharsetLibObj.getVnCharset(m_pCtrl->charsetId);
    pCharset->startOutput();

    for (i = m_changePos; i <= m_current; i++) {
        if (m_buffer[i].vnSym != vnl_nonVnChar) {
            // process vn symbol
            stdChar = m_buffer[i].vnSym + VnStdCharOffset;
            if (m_buffer[i].caps)
                stdChar--;
            if (m_buffer[i].tone != 0)
                stdChar += m_buffer[i].tone * 2;
        } else {
            stdChar = IsoToStdVnChar(m_buffer[i].keyCode);
        }

        if (stdChar != INVALID_STD_CHAR)
            ret = pCharset->putChar(os, stdChar, bytesWritten);
    }

    outSize = os.getOutBytes();
    return (ret ? 0 : VNCONV_OUT_OF_MEMORY);
}

//---------------------------------------------
// Returns the number of backspaces needed to
// go back from last to first
//---------------------------------------------
int UkEngine::getSeqSteps(int first, int last) const {
    StdVnChar stdChar;

    if (last < first)
        return 0;

    if (m_pCtrl->charsetId == CONV_CHARSET_XUTF8 ||
        m_pCtrl->charsetId == CONV_CHARSET_UNICODE)
        return (last - first + 1);

    StringBOStream os(0, 0);
    int i, bytesWritten;

    VnCharset *pCharset = VnCharsetLibObj.getVnCharset(m_pCtrl->charsetId);
    pCharset->startOutput();

    for (i = first; i <= last; i++) {
        if (m_buffer[i].vnSym != vnl_nonVnChar) {
            // process vn symbol
            stdChar = m_buffer[i].vnSym + VnStdCharOffset;
            if (m_buffer[i].caps)
                stdChar--;
            if (m_buffer[i].tone != 0)
                stdChar += m_buffer[i].tone * 2;
        } else {
            stdChar = m_buffer[i].keyCode;
        }

        if (stdChar != INVALID_STD_CHAR)
            pCharset->putChar(os, stdChar, bytesWritten);
    }

    int len = os.getOutBytes();
    if (m_pCtrl->charsetId == CONV_CHARSET_UNIDECOMPOSED)
        len = len / 2;
    return len;
}

//---------------------------------------------
void UkEngine::markChange(int pos) {
    if (pos < m_changePos) {
        m_backs += getSeqSteps(pos, m_changePos - 1);
        m_changePos = pos;
    }
}

//----------------------------------------------------------------
// Called from processBackspace to keep
// character buffer (m_buffer) and key stroke buffer in synch
//----------------------------------------------------------------
void UkEngine::synchKeyStrokeBuffer() {
    // synchronize with key-stroke buffer
    if (m_keyCurrent >= 0)
        m_keyCurrent--;
    if (m_current >= 0 && m_buffer[m_current].form == vnw_empty) {
        // in character buffer, we have reached a word break,
        // so we also need to move key stroke pointer backward to corresponding
        // word break
        while (m_keyCurrent >= 0 &&
               m_keyStrokes[m_keyCurrent].ev.chType != ukcWordBreak) {
            m_keyCurrent--;
        }
    }
}

//---------------------------------------------
int UkEngine::processBackspace(int &backs, unsigned char *outBuf, int &outSize,
                               UkOutputType &outType) {
    outType = UkCharOutput;
    if (!m_pCtrl->vietKey || m_current < 0) {
        backs = 0;
        outSize = 0;
        return 0;
    }

    m_backs = 0;
    m_changePos = m_current + 1;
    markChange(m_current);

    if (m_current == 0 || m_buffer[m_current].form == vnw_empty ||
        m_buffer[m_current].form == vnw_nonVn ||
        m_buffer[m_current].form == vnw_c ||
        m_buffer[m_current - 1].form == vnw_c ||
        m_buffer[m_current - 1].form == vnw_cvc ||
        m_buffer[m_current - 1].form == vnw_vc) {

        m_current--;
        backs = m_backs;
        outSize = 0;
        synchKeyStrokeBuffer();
        return (backs > 1);
    }

    VowelSeq vs, newVs;
    int curTonePos, newTonePos, tone, vStart, vEnd;

    vEnd = m_current - m_buffer[m_current].vOffset;
    vs = m_buffer[vEnd].vseq;
    vStart = vEnd - VSeqList[vs].len + 1;
    newVs = m_buffer[m_current - 1].vseq;
    curTonePos = vStart + getTonePosition(vs, vEnd == m_current);
    newTonePos = vStart + getTonePosition(newVs, true);
    tone = m_buffer[curTonePos].tone;

    if (tone == 0 || curTonePos == newTonePos ||
        (curTonePos == m_current && m_buffer[m_current].tone != 0)) {
        m_current--;
        backs = m_backs;
        outSize = 0;
        synchKeyStrokeBuffer();
        return (backs > 1);
    }

    markChange(newTonePos);
    m_buffer[newTonePos].tone = tone;
    markChange(curTonePos);
    m_buffer[curTonePos].tone = 0;
    m_current--;
    synchKeyStrokeBuffer();
    backs = m_backs;
    writeOutput(outBuf, outSize);
    return 1;
}

//------------------------------------------------
void UkEngine::reset() {
    m_current = -1;
    m_keyCurrent = -1;
    m_singleMode = false;
    m_toEscape = false;
}

//------------------------------------------------
void UkEngine::resetKeyBuf() { m_keyCurrent = -1; }

//------------------------------------------------
UkEngine::UkEngine() {
    if (!m_classInit) {
        engineClassInit();
        m_classInit = true;
    }
    m_pCtrl = 0;
    m_bufSize = MAX_UK_ENGINE;
    m_keyBufSize = MAX_UK_ENGINE;
    m_current = -1;
    m_keyCurrent = -1;
    m_singleMode = false;
    m_keyCheckFunc = 0;
    m_reverted = false;
    m_toEscape = false;
    m_keyRestored = false;
}

//----------------------------------------------------
// make sure there are at least 10 entries available
//----------------------------------------------------
void UkEngine::prepareBuffer() {
    int rid;
    // prepare symbol buffer
    if (m_current >= 0 && m_current + 10 >= m_bufSize) {
        // Get rid of at least half of the current entries
        // don't get rid from the middle of a word.
        for (rid = m_current / 2;
             m_buffer[rid].form != vnw_empty && rid < m_current; rid++)
            ;
        if (rid == m_current) {
            m_current = -1;
        } else {
            rid++;
            memmove(m_buffer, m_buffer + rid,
                    (m_current - rid + 1) * sizeof(WordInfo));
            m_current -= rid;
        }
    }

    // prepare key stroke buffer
    if (m_keyCurrent > 0 && m_keyCurrent + 1 >= m_keyBufSize) {
        // Get rid of at least half of the current entries
        rid = m_keyCurrent / 2;
        memmove(m_keyStrokes, m_keyStrokes + rid,
                (m_keyCurrent - rid + 1) * sizeof(m_keyStrokes[0]));
        m_keyCurrent -= rid;
    }
}

#define ENTER_CHAR 13
enum VnCaseType { VnCaseNoChange, VnCaseAllCapital, VnCaseAllSmall };

//----------------------------------------------------
int UkEngine::macroMatch(UkKeyEvent &ev) {
    int capsLockOn = 0;
    int shiftPressed = 0;
    if (m_keyCheckFunc)
        m_keyCheckFunc(&shiftPressed, &capsLockOn);

    if (shiftPressed && (ev.keyCode == ' ' || ev.keyCode == ENTER_CHAR))
        return 0;

    const StdVnChar *pMacText = NULL;
    StdVnChar key[MAX_MACRO_KEY_LEN + 1];
    StdVnChar *pKeyStart;

    // Use static macro text so we can gain a bit of performance
    // by avoiding memory allocation each time this function is called
    static StdVnChar macroText[MAX_MACRO_TEXT_LEN + 1];

    int i, j;

    i = m_current;
    while (i >= 0 && (m_current - i + 1) < MAX_MACRO_KEY_LEN) {
        while (i >= 0 && m_buffer[i].form != vnw_empty &&
               (m_current - i + 1) < MAX_MACRO_KEY_LEN)
            i--;
        if (i >= 0 && m_buffer[i].form != vnw_empty)
            return 0;

        if (i >= 0) {
            if (m_buffer[i].vnSym != vnl_nonVnChar) {
                key[0] = m_buffer[i].vnSym + VnStdCharOffset;
                if (m_buffer[i].caps)
                    key[0]--;
                key[0] += m_buffer[i].tone * 2;
            } else
                key[0] = m_buffer[i].keyCode;
        }

        for (j = i + 1; j <= m_current; j++) {
            if (m_buffer[j].vnSym != vnl_nonVnChar) {
                key[j - i] = m_buffer[j].vnSym + VnStdCharOffset;
                if (m_buffer[j].caps)
                    key[j - i]--;
                key[j - i] += m_buffer[j].tone * 2;
            } else
                key[j - i] = m_buffer[j].keyCode;
        }
        key[m_current - i + 1] = 0;
        // search macro table
        pMacText = m_pCtrl->macStore.lookup(key + 1);
        if (pMacText) {
            i++; // mark the position where change is needed
            pKeyStart = key + 1;
            break;
        }
        if (i >= 0) {
            pMacText = m_pCtrl->macStore.lookup(key);
            if (pMacText) {
                pKeyStart = key;
                break;
            }
        }
        i--;
    }

    if (!pMacText) {
        return 0;
    }

    markChange(i);

    // determine the form of macro replacements: ALL CAPITALS, First Character
    // Capital, or no change
    VnCaseType macroCase;
    if (IS_STD_VN_LOWER(*pKeyStart)) {
        macroCase = VnCaseAllSmall;
    } else if (IS_STD_VN_UPPER(*pKeyStart)) {
        macroCase = VnCaseAllCapital;
        for (i = 1; pKeyStart[i]; i++) {
            if (IS_STD_VN_LOWER(pKeyStart[i])) {
                macroCase = VnCaseNoChange;
            }
        }
    } else
        macroCase = VnCaseNoChange;

    // Convert case of macro text according to macroCase
    int charCount = 0;
    while (pMacText[charCount] != 0)
        charCount++;

    for (i = 0; i < charCount; i++) {
        if (macroCase == VnCaseAllCapital)
            macroText[i] = StdVnToUpper(pMacText[i]);
        else if (macroCase == VnCaseAllSmall)
            macroText[i] = StdVnToLower(pMacText[i]);
        else
            macroText[i] = pMacText[i];
    }

    // Convert to target output charset
    int outSize;
    int maxOutSize = *m_pOutSize;
    int inLen = charCount * sizeof(StdVnChar);
    VnConvert(CONV_CHARSET_VNSTANDARD, m_pCtrl->charsetId, (UKBYTE *)macroText,
              (UKBYTE *)m_pOutBuf, &inLen, &maxOutSize);
    outSize = maxOutSize;

    // write the last input character
    StdVnChar vnChar;
    if (outSize < *m_pOutSize && ev.keyCode) {
        maxOutSize = *m_pOutSize - outSize;
        if (ev.vnSym != vnl_nonVnChar)
            vnChar = ev.vnSym + VnStdCharOffset;
        else
            vnChar = ev.keyCode;
        inLen = sizeof(StdVnChar);
        VnConvert(CONV_CHARSET_VNSTANDARD, m_pCtrl->charsetId,
                  (UKBYTE *)&vnChar, ((UKBYTE *)m_pOutBuf) + outSize, &inLen,
                  &maxOutSize);
        outSize += maxOutSize;
    }
    int backs = m_backs; // store m_backs before calling reset
    reset();
    m_outputWritten = true;
    m_backs = backs;
    *m_pOutSize = outSize;
    return 1;
}

//----------------------------------------------------
int UkEngine::restoreKeyStrokes(int &backs, unsigned char *outBuf, int &outSize,
                                UkOutputType &outType) {
    outType = UkKeyOutput;
    if (!lastWordHasVnMark()) {
        backs = 0;
        outSize = 0;
        return 0;
    }

    m_backs = 0;
    m_changePos = m_current + 1;

    int keyStart;
    bool converted = false;
    for (keyStart = m_keyCurrent;
         keyStart >= 0 && m_keyStrokes[keyStart].ev.chType != ukcWordBreak;
         keyStart--) {
        if (m_keyStrokes[keyStart].converted) {
            converted = true;
        }
    }
    keyStart++;

    if (!converted) {
        // no key stroke has been converted, so it doesn't make sense to restore
        // key strokes
        backs = 0;
        outSize = 0;
        return 0;
    }

    // int i = m_current;
    while (m_current >= 0 && m_buffer[m_current].form != vnw_empty)
        m_current--;
    markChange(m_current + 1);
    backs = m_backs;

    int count;
    int i;
    UkKeyEvent ev;
    m_keyRestoring = true;
    for (i = keyStart, count = 0; i <= m_keyCurrent; i++) {
        if (count < outSize) {
            outBuf[count++] = (unsigned char)m_keyStrokes[i].ev.keyCode;
        }
        m_pCtrl->input.keyCodeToSymbol(m_keyStrokes[i].ev.keyCode, ev);
        m_keyStrokes[i].converted = false;
        processAppend(ev);
    }
    outSize = count;
    m_keyRestoring = false;

    return 1;
}

//--------------------------------------------------
void UkEngine::setSingleMode() { m_singleMode = true; }

//--------------------------------------------------
static void SetupUnikeyEngineOnce() {
    SetupInputClassifierTable();
    int i;
    VnLexiName lexi;

    // Calculate IsoStdVnCharMap
    for (i = 0; i < 256; i++) {
        IsoStdVnCharMap[i] = i;
    }

    for (i = 0; SpecialWesternChars[i]; i++) {
        IsoStdVnCharMap[SpecialWesternChars[i]] =
            (vnl_lastChar + i) + VnStdCharOffset;
    }

    for (i = 0; i < 256; i++) {
        if ((lexi = IsoToVnLexi(i)) != vnl_nonVnChar) {
            IsoStdVnCharMap[i] = lexi + VnStdCharOffset;
        }
    }
}

std::once_flag setupFlag;

void SetupUnikeyEngine() { std::call_once(setupFlag, SetupUnikeyEngineOnce); }

//--------------------------------------------------
bool UkEngine::atWordBeginning() const {
    return (m_current < 0 || m_buffer[m_current].form == vnw_empty);
}

//--------------------------------------------------
// Check for macro first, if there's a match, expand macro. If not:
// Spell-check, if is valid Vietnamese, return normally, if not:
// restore key strokes if auto-restore is enabled
//--------------------------------------------------
int UkEngine::processWordEnd(UkKeyEvent &ev) {
    if (m_pCtrl->options.macroEnabled && macroMatch(ev))
        return 1;

    auto putKeyInBuffer = [this](UkKeyEvent &ev) {
        m_current++;
        WordInfo &entry = m_buffer[m_current];
        entry.form = vnw_empty;
        entry.c1Offset = entry.c2Offset = entry.vOffset = -1;
        entry.keyCode = ev.keyCode;
        entry.vnSym = vnToLower(ev.vnSym);
        entry.caps = (entry.vnSym != ev.vnSym);
    };

    if (!m_pCtrl->options.spellCheckEnabled || m_singleMode || m_current < 0 ||
        m_keyRestoring) {
        putKeyInBuffer(ev);
        return 0;
    }

    int outSize = 0;
    if (m_pCtrl->options.autoNonVnRestore && lastWordIsNonVn()) {
        outSize = *m_pOutSize;
        if (restoreKeyStrokes(m_backs, m_pOutBuf, outSize, m_outType)) {
            m_keyRestored = true;
            m_outputWritten = true;
        }
    }

    putKeyInBuffer(ev);

    if (m_keyRestored && outSize < *m_pOutSize) {
        if (ev.keyCode) {
            m_pOutBuf[outSize] = ev.keyCode;
            outSize++;
        }
        *m_pOutSize = outSize;
        return 1;
    }

    return 0;
}

//---------------------------------------------------------------------------
// Test if last word is a non-Vietnamese word, so that
// the engine can restore key strokes if it is indeed not a Vietnamese word
//---------------------------------------------------------------------------
bool UkEngine::lastWordIsNonVn() const {
    if (m_current < 0)
        return false;

    switch (m_buffer[m_current].form) {
    case vnw_nonVn:
        return true;
    case vnw_empty:
    case vnw_c:
        return false;
    case vnw_v:
    case vnw_cv:
        return !VSeqList[m_buffer[m_current].vseq].complete;
    case vnw_vc:
    case vnw_cvc: {
        int vIndex = m_current - m_buffer[m_current].vOffset;
        VowelSeq vs = m_buffer[vIndex].vseq;
        if (!VSeqList[vs].complete)
            return true;
        ConSeq cs = m_buffer[m_current].cseq;
        ConSeq c1 = cs_nil;
        if (m_buffer[m_current].c1Offset != -1)
            c1 = m_buffer[m_current - m_buffer[m_current].c1Offset].cseq;

        if (!isValidCVC(c1, vs, cs)) {
            return true;
        }

        int tonePos =
            (vIndex - VSeqList[vs].len + 1) + getTonePosition(vs, false);
        int tone = m_buffer[tonePos].tone;
        if ((cs == cs_c || cs == cs_ch || cs == cs_p || cs == cs_t) &&
            (tone == 2 || tone == 3 || tone == 4)) {
            return true;
        }
    }
    }
    return false;
}

//---------------------------------------------------------------------------
// Test if last word has a Vietnamese mark, that is tones, decorators
//---------------------------------------------------------------------------
bool UkEngine::lastWordHasVnMark() const {
    for (int i = m_current; i >= 0 && m_buffer[i].form != vnw_empty; i--) {
        VnLexiName sym = m_buffer[i].vnSym;
        if (sym != vnl_nonVnChar) {
            if (IsVnVowel[sym]) {
                if (m_buffer[i].tone)
                    return true;
            }
            if (sym != StdVnRootChar[sym])
                return true;
        }
    }
    return false;
}
