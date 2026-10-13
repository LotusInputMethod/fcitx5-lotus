/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file input_method.h
 * @brief Builds an InputMethod from the generated definitions, ported 1:1 from
 *        bamboo-core/input_method_def.go (ParseInputMethod).
 */

#ifndef _LOTUS_INPUT_METHOD_H_
#define _LOTUS_INPUT_METHOD_H_

#include "rule.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lotus {

    /**
     * @brief Builds an input method from an explicit definition map, like Go's ParseInputMethod.
     * @param definition Key/value pairs of the definition (input key -> DSL line).
     * @param name Definition name to store in the result ("Custom", "Telex", ...).
     * @return Parsed input method; an empty rule set when the definition has no usable line.
     */
    InputMethod parseInputMethod(const std::unordered_map<std::string, std::string>& definition, std::string_view name);

    /**
     * @brief Builds the input method named by name, like Go's ParseInputMethod.
     * @param name Definition name ("Telex", "VNI", "VIQR", ...).
     * @return Parsed input method; a default (empty name, no rules) InputMethod when unknown.
     */
    InputMethod parseInputMethod(std::string_view name);

    /**
     * @brief Input method named by name, parsed once and shared by every caller.
     * @param name Definition name ("Telex", "VNI", "VIQR", ...).
     * @return Parsed input method, the same object for repeated calls; a reference to a default
     *         (empty name, no rules) InputMethod when the name is unknown.
     */
    const InputMethod& cachedInputMethod(std::string_view name);

    /**
     * @brief Names of every generated input method, in the order the addon menu shows them.
     *
     * Go sorts the definition names with an explicit order table (bamboo-c.go:279-287) because
     * the definitions themselves live in a map. The seven names below are that table, in order.
     * The list is built once and reused by every call.
     * @return Input method names, menu order first.
     */
    const std::vector<std::string>& inputMethodNames();

} // namespace lotus

#endif // _LOTUS_INPUT_METHOD_H_
