/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-mode.h
 * @brief Typing modes and per-app rule settings shared by the config, the engine and the state.
 */

#ifndef _FCITX5_LOTUS_MODE_H_
#define _FCITX5_LOTUS_MODE_H_

#include <cstdint>

namespace fcitx {

    /**
     * @brief Operating modes for the Lotus input method.
     */
    enum class LotusMode : std::uint8_t {
        Off,
        Smooth,
        SuperSmooth,
        SurroundingText,
        Preedit,
        Emoji,
    };

    /**
     * @brief Resolved per-app rule: typing mode plus optional delay overrides (0 = use global default).
     */
    struct LotusAppRuleSetting {
        LotusMode mode              = LotusMode::Smooth; ///< Typing mode for the app
        int       commitInterval    = 0;                 ///< ms cooldown between commits (KbMsg.preDelay); 0 = default
        int       backspaceInterval = 0;                 ///< ms between injected keys (KbMsg.interval); 0 = default
        int       postDelay         = 0;                 ///< ms before the final trigger key (KbMsg.postDelay); 0 = default
    };

    /**
     * @brief Converts LotusMode to int and vice versa.
     */
    int       modeToInt(LotusMode mode);
    LotusMode intToMode(int mode);

} // namespace fcitx

#endif // _FCITX5_LOTUS_MODE_H_
