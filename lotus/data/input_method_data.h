/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file input_method_data.h
 * @brief Input method definitions ported from bamboo-core (see CREDITS)
 */
#ifndef _LOTUS_GENERATED_INPUT_METHOD_DATA_H_
#define _LOTUS_GENERATED_INPUT_METHOD_DATA_H_

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace lotus::data {
    /** @brief Input methods, in the order the addon lists them; Custom has no built-in definition. */
    enum class InputMethodId : std::uint8_t {
        Telex,           ///< "Telex"
        Vni,             ///< "VNI"
        TelexVni,        ///< "Telex + VNI"
        TelexVniViqr,    ///< "Telex + VNI + VIQR"
        Viqr,            ///< "VIQR"
        MicrosoftLayout, ///< "Microsoft layout"
        VniFrench,       ///< "VNI Bàn phím tiếng Pháp"
        Custom,          ///< "Custom"
    };

    /** @brief Number of InputMethodId values, for loops over the enum. */
    inline constexpr std::size_t kInputMethodCount = 8U;

    /** @brief input method -> (key -> dsl line), indexed by InputMethodId; the Custom entry is empty. */
    const std::array<std::unordered_map<std::string, std::string>, kInputMethodCount>& inputMethodDefinitions();
} // namespace lotus::data

#endif // _LOTUS_GENERATED_INPUT_METHOD_DATA_H_
