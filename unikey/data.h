/*
 * SPDX-FileCopyrightText: 1998-2002 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VIETNAMESE_CHARSET_DATA_H
#define VIETNAMESE_CHARSET_DATA_H

// This header defines some special characters
const StdVnChar StdStartQuote =
    (VnStdCharOffset + 201); // 0x93 in the Western charset
// 201 is the offset of character 0x93 (start quote) in Vn charsets
const StdVnChar StdEndQuote =
    (VnStdCharOffset + 202); // 0x94 in the Western charset
const StdVnChar StdEllipsis =
    (VnStdCharOffset + 190); // 0x85 in Western charet.

#endif
