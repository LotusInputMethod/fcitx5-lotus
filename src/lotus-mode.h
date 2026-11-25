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
    inline int modeToInt(LotusMode mode) {
        switch (mode) {
            case LotusMode::Off: return 0;
            case LotusMode::Smooth: return 1;
            case LotusMode::SuperSmooth: return 3;
            case LotusMode::SurroundingText: return 4;
            case LotusMode::Preedit: return 5;
            case LotusMode::Emoji: return 6;
        }
        return 0;
    }

    inline LotusMode intToMode(int mode) {
        switch (mode) {
            case 1:
            case 2:
            case 7: return LotusMode::Smooth;
            case 3: return LotusMode::SuperSmooth;
            case 4: return LotusMode::SurroundingText;
            case 5: return LotusMode::Preedit;
            case 6: return LotusMode::Emoji;
            case 0:
            default: return LotusMode::Off;
        }
    }

} // namespace fcitx

#endif // _FCITX5_LOTUS_MODE_H_
