/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _LOTUS_PROTOCOL_H_
#define _LOTUS_PROTOCOL_H_

#include <cstdint>

namespace fcitx {

/**
 * @brief Single key event on the keyboard socket (addon -> uinput server).
 *
 * Minimal dumb pipe: code + pressed state only. No counts, no delays —
 * all pacing lives in KeyPacer on the client via fcitx EventLoop timers.
 */
struct KeyMsg {
    uint16_t code;   ///< linux KEY_*: BACKSPACE / LEFT / DELETE / LEFTSHIFT
    uint8_t  pressed; ///< 1 = press, 0 = release
};

} // namespace fcitx

#endif // _LOTUS_PROTOCOL_H_
