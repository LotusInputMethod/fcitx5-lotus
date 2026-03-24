/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file ack-apps.h
 * @brief List of applications requiring acknowledgment workaround.
 *
 * These browsers need special handling for uinput mode to work correctly.
 */

#include <string>
#include <vector>

/**
 * @brief List of application names requiring ACK workaround.
 *
 * Chromium-based browsers that need special handling for text replacement.
 */
static std::vector<std::string> ack_apps = {"chrome", "chromium", "brave", "edge", "vivaldi", "opera", "coccoc", "cromite", "helium", "thorium", "slimjet", "yandex"};

/**
 * @brief List of terminal emulator names requiring commit delay.
 *
 * Terminal emulators running TUI apps may lose committed text due to timing
 * mismatch between uinput backspaces and IM protocol commits.
 */
static std::vector<std::string> terminal_apps = {
    "gnome-terminal", "kgx", "konsole", "alacritty", "kitty", "foot",
    "wezterm", "tilix", "terminator", "xterm", "urxvt", "st",
    "sakura", "xfce4-terminal", "mate-terminal", "lxterminal",
    "terminology", "contour", "blackbox", "ghostty",
    "code", "codium", "cursor",
};
