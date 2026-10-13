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

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/un.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>

#include "lotus-config.h"
#include "lotus-export.h"

/**
 * @brief Maximum length of Unix socket paths.
*/
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un*)0)->sun_path)

// The log category lives inside namespace fcitx: a global "lotus" function would collide with the
// lotus:: engine namespace, which the addon includes since it drives the C++ engine directly.
namespace fcitx {
    FCITX_DECLARE_LOG_CATEGORY(lotusLog);
}

#define LOTUS_DEBUG(msg) FCITX_LOGC(fcitx::lotusLog, Debug) << "[DEBUG] " << msg
#define LOTUS_INFO(msg)  FCITX_LOGC(fcitx::lotusLog, Info) << "[INFO] " << msg
#define LOTUS_WARN(msg)  FCITX_LOGC(fcitx::lotusLog, Warn) << "[WARN] " << msg
#define LOTUS_ERROR(msg) FCITX_LOGC(fcitx::lotusLog, Error) << "[ERROR] " << msg

// Forward declaration for fcitx types
using KeySym = uint32_t;

// Global state variables for input processing
// LOTUS_EXPORT: read by the headless tests through lotus_test_core.
extern LOTUS_EXPORT std::atomic<fcitx::LotusMode> realMode;          ///< Current active input mode
extern std::atomic<bool>                          g_mouse_clicked;   ///< Mouse click detection flag
extern std::atomic<bool>                          is_deleting_;      ///< Deletion in progress flag
extern std::atomic<bool>                          stop_flag_monitor; ///< Signal to stop monitor threads
extern std::atomic<int>                           uinput_client_fd_; ///< Uinput client file descriptor
extern std::atomic<unsigned int>                  realtextLen;       ///< Current text length
extern std::atomic<int>                           mouse_socket_fd;   ///< Mouse socket file descriptor

/**
 * @brief Builds socket path from base suffix.
 * @param base_path_suffix Suffix to append to base path.
 * @return Full socket path.
 */
std::string LOTUS_EXPORT buildSocketPath(std::string_view base_path_suffix);

/**
 * @brief Creates a non-blocking abstract-namespace UNIX socket.
 *
 * Fills @p addr with the abstract socket name and @p len with the address
 * length. The caller keeps ownership of the returned descriptor and is
 * responsible for connect()/bind() and for closing it on failure.
 *
 * @param name Socket name inside the abstract namespace.
 * @param addr Destination address.
 * @param len Destination address length.
 * @return Socket descriptor, or -1 when the socket could not be created.
 */
int makeAbstractUnixSocket(std::string_view name, struct sockaddr_un& addr, socklen_t& len);

/**
 * @brief Closes the uinput client socket and marks the client as disconnected.
 */
void closeUinputClient();

/**
 * @brief Returns the fcitx5 configuration directory of the current user.
 * @return Path of the fcitx5 configuration directory.
 */
std::string lotusConfigDir();

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
 * @brief Key value handed to the engine for a key symbol.
 *
 * The Go engine took key symbols (BackSpace, Space and Tab are symbols, not code points), so a symbol
 * with a Unicode mapping is passed as its code point and every other symbol as-is.
 * @param sym Key symbol of the event.
 * @return Key value for lotus::KeyProcessor::processKey.
 */
uint32_t engineKeyValue(uint32_t sym);

/**
 * @brief Whether a mode delivers text through the uinput (fake backspace) path.
 * @param mode Mode to check.
 * @return True for Uinput, Smooth, SuperSmooth, Minecraft and Select.
 */
bool isUinputMode(fcitx::LotusMode mode);

/**
 * @brief Compares two texts and computes diff.
 * @param A First text.
 * @param B Second text.
 * @param deletedPart Output deleted portion.
 * @param addedPart Output added portion.
 * @return Comparison result code.
 */
int compareAndSplitU32(std::u32string_view A, std::u32string_view B, std::u32string& deletedPart, std::u32string& addedPart);

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

/**
 * @brief get environement variable.
 * @param name Name of the variable.
 * @return Value of the variable.
 */
std::string LOTUS_EXPORT getEnv(const std::string& name);

#endif // _FCITX5_LOTUS_UTILS_H_
