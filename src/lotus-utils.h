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
#if defined(LOTUS_ENABLE_LOG)
#include <fcitx-utils/log.h>
#endif
#include <fcitx/inputcontext.h>

#include "lotus-config.h"
#if defined(LOTUS_ENABLE_AVX512) && defined(__AVX512F__)
extern "C" size_t compare_split_avx512(
    const char* A, const char* B, size_t lenA, size_t lenB, void* dummy);
extern "C" size_t utf8_length_avx512(const char* str, size_t len);
extern "C" size_t find_char_avx512(const char* str, size_t len, size_t start, int ch);
extern "C" size_t compare_split_avx512(
    const char* A,
    const char* B,
    size_t lenA,
    size_t lenB,
    void* dummy
);
extern "C" size_t tolower_avx512(char* str, size_t len);
extern "C" size_t strfind_avx512(const char* hay, size_t hlen, const char* needle, size_t nlen);
#endif

/**
 * @brief Maximum length of Unix socket paths.
*/
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un*)0)->sun_path)

FCITX_DECLARE_LOG_CATEGORY(lotus);

#if defined(LOTUS_ENABLE_LOG)
#define LOTUS_DEBUG(msg) FCITX_LOGC(lotus, Debug) << "[DEBUG] " << msg
#define LOTUS_INFO(msg)  FCITX_LOGC(lotus, Info) << "[INFO] " << msg
#define LOTUS_WARN(msg)  FCITX_LOGC(lotus, Warn) << "[WARN] " << msg
#define LOTUS_ERROR(msg) FCITX_LOGC(lotus, Error) << "[ERROR] " << msg
#else
#define LOTUS_DEBUG(msg) ((void)0)
#define LOTUS_INFO(msg)  ((void)0)
#define LOTUS_WARN(msg)  ((void)0)
#define LOTUS_ERROR(msg) ((void)0)
#endif
// Forward declaration for fcitx types
using KeySym = uint32_t;

// Global state variables for input processing
extern std::atomic<fcitx::LotusMode> realMode;               ///< Current active input mode
extern std::atomic<bool>             needEngineReset;        ///< Flag to trigger engine reset
extern std::atomic<bool>             g_mouse_clicked;        ///< Mouse click detection flag
extern std::atomic<bool>             is_deleting_;           ///< Deletion in progress flag
extern std::atomic<bool>             stop_flag_monitor;      ///< Signal to stop monitor threads
extern std::atomic<bool>             monitor_running;        ///< Monitor thread status
extern std::atomic<int>              uinput_client_fd_;      ///< Uinput client file descriptor
extern std::atomic<unsigned int>     realtextLen;            ///< Current text length
extern std::atomic<int>              mouse_socket_fd;        ///< Mouse socket file descriptor
extern std::atomic<int64_t>          replacement_start_ms_;  ///< Timestamp for replacement
extern std::atomic<int>              replacement_thread_id_; ///< Thread ID for replacement
extern std::atomic<bool>             needFallbackCommit;     ///< Fallback commit flag
extern std::mutex                    monitor_mutex;          ///< Mutex for monitor synchronization
extern std::condition_variable       monitor_cv;             ///< Condition variable for monitor

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
int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& deletedPart, std::string& addedPart);

/**
 * @brief Checks if string starts with prefix.
 * @param str String to check.
 * @param prefix Prefix to check.
 * @return True if string starts with prefix.
 */
bool isStartsWith(const std::string& str, const std::string& prefix);

/**
 * @brief Get the frontend name from the input context.
 * @param ic Input context.
 * @return Frontend name.
 */
std::string getFrontendName(fcitx::InputContext* ic);

/**
 * @brief Key event entry for replay buffer.
 */
struct KeyEntry {
    uint32_t sym;   ///< Key symbol
    uint32_t state; ///< Key state (modifiers)
};

#endif // _FCITX5_LOTUS_UTILS_H_
