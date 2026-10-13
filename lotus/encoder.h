/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file encoder.h
 * @brief Converts engine output into the configured output charset, ported 1:1 from
 *        bamboo-core/encoder.go.
 */

#ifndef _LOTUS_ENCODER_H_
#define _LOTUS_ENCODER_H_

#include <string>
#include <string_view>
#include <vector>

namespace lotus {

    /** @brief Name of the UTF-8 charset, 1:1 with Go's UNICODE constant. */
    inline constexpr std::string_view kUnicodeCharset = "Unicode";

    /**
     * @brief Builds the charset name list from the generated table, "Unicode" first.
     * @return Charset names: the Unicode charset, then every table name sorted ascending.
     */
    std::vector<std::string> buildCharsetNames();

    /**
     * @brief Names of every supported charset, "Unicode" first then the rest sorted ascending.
     * @return Charset names, 1:1 with Go's GetCharsetNames, built once and reused by every call.
     */
    const std::vector<std::string>& charsetNames();

    /**
     * @brief Encodes engine text into a charset, like Go's Encode.
     * @param charsetName Charset name; "Unicode" and unknown names produce UTF-8.
     * @param text Engine text in UTF-32.
     * @return Encoded string (UTF-8 for Unicode, the legacy bytes of the table otherwise).
     */
    std::string encode(std::string_view charsetName, std::u32string_view text);

} // namespace lotus

#endif // _LOTUS_ENCODER_H_
