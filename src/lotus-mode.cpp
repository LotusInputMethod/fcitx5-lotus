/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */


#include "lotus-mode.h"

namespace fcitx {

    int modeToInt(LotusMode mode) {
        switch (mode) {
            case LotusMode::Off: return 0;
            case LotusMode::Smooth: return 1;
            case LotusMode::SuperSmooth: return 3;
            case LotusMode::SurroundingText: return 4;
            case LotusMode::Preedit: return 5;
            case LotusMode::Emoji: return 6;
            case LotusMode::Select: return 7;
            default: return 0;
        }
    }

    LotusMode intToMode(int mode) {
        switch (mode) {
            case 0: return LotusMode::Off;
            case 1:
            case 2: return LotusMode::Smooth;
            case 3: return LotusMode::SuperSmooth;
            case 4: return LotusMode::SurroundingText;
            case 5: return LotusMode::Preedit;
            case 6: return LotusMode::Emoji;
            case 7: return LotusMode::Select;
            default: return LotusMode::Off;
        }
    }

} // namespace fcitx
