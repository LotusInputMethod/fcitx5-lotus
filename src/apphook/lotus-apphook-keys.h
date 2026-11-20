/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-keys.h
 * @brief Defines keyboard operations shared by Lotus and its injected hook.
 */

#ifndef _SRC_LOTUS_APPHOOK_KEYS_H_
#define _SRC_LOTUS_APPHOOK_KEYS_H_

#include <cstdint>

namespace lotus::apphook {
    constexpr uint32_t kModifierShift = 1;
    constexpr uint32_t kModifierCtrl  = 2;
    constexpr uint32_t kModifierAlt   = 4;
    constexpr uint32_t kModifierSuper = 8;

    /**
     * @brief One logical key operation to repeat inside the target process.
     */
    struct KeyOperation {
        uint32_t keycode;   ///< Linux evdev KEY_* code
        uint32_t modifiers; ///< kModifier* bitmask
        uint32_t count;     ///< Number of key taps
    };
} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_KEYS_H_
