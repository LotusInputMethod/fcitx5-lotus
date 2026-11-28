// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "composition.h"
#include "flattener.h"
#include "test-utils.h"

#include <source_location>
#include <string>
#include <string_view>

/** @brief Checks that flattening the composition in the mode renders the expected canvas. */
inline void checkCanvas(const char* name, const lotus::Composition& composition, lotus::ModeSet mode, std::u32string_view expected,
                        const std::source_location& location = std::source_location::current()) {
    const std::u32string actual = lotus::flatten(composition, mode);
    ::check(actual == expected, name, std::to_string(expected.size()) + " code points", std::to_string(actual.size()) + " code points",
            "the flattened canvas must match the expectation ported from the Go tests", location);
}
