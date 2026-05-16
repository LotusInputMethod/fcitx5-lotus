/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-monitor.h
 * @brief Input monitoring and timing utilities for fcitx5-lotus.
 */

#ifndef _FCITX5_LOTUS_MONITOR_H_
#define _FCITX5_LOTUS_MONITOR_H_

#include <thread>

extern std::thread mouse_thread;

/**
 * @brief Thread function for mouse press detection and reset.
 *
 * Monitors mouse events to reset input state when needed.
 */
void mousePressResetThread();

/**
 * @brief Starts the mouse reset monitoring.
 */
void startMouseReset();

#endif // _FCITX5_LOTUS_MONITOR_H_
