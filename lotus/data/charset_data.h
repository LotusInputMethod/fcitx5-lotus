/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file charset_data.h
 * @brief Charset tables ported from bamboo-core (see CREDITS)
 */
#ifndef _LOTUS_GENERATED_CHARSET_DATA_H_
#define _LOTUS_GENERATED_CHARSET_DATA_H_

#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace lotus::data {
    /** @brief Output charsets, in the order the addon lists them (Unicode first, then name order). */
    enum class Charset : std::uint8_t {
        Unicode,               ///< UTF-8, the identity charset (empty table)
        Bhkcm1,                ///< "BKHCM 1"
        Bhkcm2,                ///< "BKHCM 2"
        NcrDecimal,            ///< "NCR Decimal"
        NcrHex,                ///< "NCR Hex"
        Tcvn3Abc,              ///< "TCVN3 (ABC)"
        Utf8,                  ///< "UTF-8"
        UnicodeCStringDecimal, ///< "Unicode C string Decimal"
        UnicodeCStringHex,     ///< "Unicode C string Hex"
        UnicodeCombining,      ///< "Unicode tổ hợp"
        Viqr,                  ///< "VIQR"
        Viscii,                ///< "VISCII"
        VniWindows,            ///< "VNI Windows"
        Vps,                   ///< "VPS"
        VietwareFull,          ///< "Vietware Full"
        VietwareX,             ///< "Vietware X"
        Windows1258,           ///< "Windows 1258 codepage"
    };

    /** @brief Number of Charset values, for loops over the enum. */
    inline constexpr std::size_t kCharsetCount = 17U;

    /** @brief charset -> (code point -> encoded bytes), indexed by Charset; the Unicode entry is empty. */
    const std::array<std::unordered_map<char32_t, std::string_view>, kCharsetCount>& charsetDefinitions();
} // namespace lotus::data

#endif // _LOTUS_GENERATED_CHARSET_DATA_H_
