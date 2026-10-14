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

#include "data/charset_data.h"

#include <string>
#include <string_view>

namespace lotus {

    /** @brief Output charsets, shared with the addon configuration. */
    using data::Charset;

    /** @brief Number of Charset values, for loops over the enum. */
    using data::kCharsetCount;

    /**
     * @brief Encodes engine text into a charset, like Go's Encode.
     * @param charset Output charset.
     * @param text Engine text in UTF-32.
     * @return Encoded string (UTF-8 for Charset::Unicode, the legacy bytes of the table otherwise).
     */
    std::string encode(Charset charset, std::u32string_view text);

} // namespace lotus

#endif // _LOTUS_ENCODER_H_
