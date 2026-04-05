/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _LOTUS_KEY_COMMAND_H_
#define _LOTUS_KEY_COMMAND_H_

#include <cstdint>

enum class LotusKeyCommandType : uint32_t {
    BackspaceCount = 0,
    KeyEvent       = 1,
    QueryCapsLock  = 2,
    OskShow        = 3,
    OskHide        = 4
};

struct LotusKeyCommand {
    LotusKeyCommandType type;  // Command type
    uint32_t            code;  // Keycode or count
    uint32_t            value; // 0/1 for key event
};

#endif // _LOTUS_KEY_COMMAND_H_
