/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-utils.h
 * @brief Utility functions and global state for fcitx5-lotus.
 */

#ifndef _FCITX5_LOTUS_UTILS_H_
#define _FCITX5_LOTUS_UTILS_H_

#include <condition_variable>
#include <mutex>
#include <atomic>
#include <sys/un.h>
#include <fcitx-utils/log.h>

#include "lotus-config.h"

/**
 * @brief Maximum length of Unix socket paths.
*/
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un*)0)->sun_path)

FCITX_DECLARE_LOG_CATEGORY(lotus);

#define LOTUS_DEBUG(msg) FCITX_LOGC(lotus, Debug) << "[DEBUG] " << msg
#define LOTUS_INFO(msg)  FCITX_LOGC(lotus, Info) << "[INFO] " << msg
#define LOTUS_WARN(msg)  FCITX_LOGC(lotus, Warn) << "[WARN] " << msg
#define LOTUS_ERROR(msg) FCITX_LOGC(lotus, Error) << "[ERROR] " << msg

// Forward declaration for fcitx types
using KeySym = uint32_t;

// Global state variables for input processing
extern fcitx::LotusMode        realMode;               ///< Current active input mode
extern std::atomic<bool>       needEngineReset;        ///< Flag to trigger engine reset
extern std::string             BASE_SOCKET_PATH;       ///< Base path for Unix sockets
extern std::atomic<bool>       g_mouse_clicked;        ///< Mouse click detection flag
extern std::atomic<bool>       is_deleting_;           ///< Deletion in progress flag
extern std::once_flag          monitor_init_flag;      ///< One-time initialization flag
extern std::atomic<bool>       stop_flag_monitor;      ///< Signal to stop monitor threads
extern std::atomic<bool>       monitor_running;        ///< Monitor thread status
extern int                     uinput_client_fd_;      ///< Uinput client file descriptor
extern unsigned int            realtextLen;            ///< Current text length
extern std::atomic<int>        mouse_socket_fd;        ///< Mouse socket file descriptor
extern std::atomic<int64_t>    replacement_start_ms_;  ///< Timestamp for replacement
extern std::atomic<int>        replacement_thread_id_; ///< Thread ID for replacement
extern std::atomic<bool>       needFallbackCommit;     ///< Fallback commit flag
extern std::mutex              monitor_mutex;          ///< Mutex for monitor synchronization
extern std::condition_variable monitor_cv;             ///< Condition variable for monitor

/**
 * @brief Builds socket path from base suffix.
 * @param base_path_suffix Suffix to append to base path.
 * @return Full socket path.
 */
std::string buildSocketPath(const char* base_path_suffix);

/**
 * @brief Gets current time in milliseconds.
 * @return Timestamp in milliseconds.
 */
int64_t now_ms();

/**
 * @brief Checks if key symbol is a backspace.
 * @param sym Key symbol to check.
 * @return True if backspace.
 */
bool isBackspace(uint32_t sym);

/**
 * @brief Compares two strings and computes diff.
 * @param A First string.
 * @param B Second string.
 * @param commonPrefix Output common prefix.
 * @param deletedPart Output deleted portion.
 * @param addedPart Output added portion.
 * @return Comparison result code.
 */
int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& commonPrefix, std::string& deletedPart, std::string& addedPart);

/**
 * @brief Checks if string starts with prefix.
 * @param str String to check.
 * @param prefix Prefix to check.
 * @return True if string starts with prefix.
 */
bool isStartsWith(const std::string& str, const std::string& prefix);

/**
 * @brief Key event entry for replay buffer.
 */
struct KeyEntry {
    uint32_t sym;   ///< Key symbol
    uint32_t state; ///< Key state (modifiers)
};

#endif // _FCITX5_LOTUS_UTILS_H_
