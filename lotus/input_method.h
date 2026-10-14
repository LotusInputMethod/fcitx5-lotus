/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file input_method.h
 * @brief Builds an InputMethod from the definition tables, ported 1:1 from
 *        bamboo-core/input_method_def.go (ParseInputMethod).
 */

#ifndef _LOTUS_INPUT_METHOD_H_
#define _LOTUS_INPUT_METHOD_H_

#include "data/input_method_data.h"
#include "rule.h"

#include <string>
#include <unordered_map>

namespace lotus {

    /** @brief Built-in input methods, shared with the addon configuration. */
    using data::InputMethodId;

    /** @brief Number of InputMethodId values, for loops over the enum. */
    using data::kInputMethodCount;

    /**
     * @brief Builds an input method from an explicit definition map, like Go's ParseInputMethod.
     * @param definition Key/value pairs of the definition (input key -> DSL line).
     * @return Parsed input method; an empty rule set when the definition has no usable line.
     */
    InputMethod parseInputMethod(const std::unordered_map<std::string, std::string>& definition);

    /**
     * @brief Input method parsed once and shared by every caller, indexed by the enum.
     * @param id Built-in input method; Custom yields the default (empty) input method.
     * @return The parsed input method; the same object for repeated calls.
     */
    [[nodiscard]] const InputMethod& cachedInputMethod(InputMethodId id);

} // namespace lotus

#endif // _LOTUS_INPUT_METHOD_H_
