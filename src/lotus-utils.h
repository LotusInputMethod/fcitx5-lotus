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
#include <sys/socket.h>
#include <sys/un.h>

#include "common-export.h"
#include "lotus-mode.h"

/**
 * @brief Maximum length of Unix socket paths.
*/
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un*)0)->sun_path)

// Forward declaration: the header only passes InputContext pointers around.
namespace fcitx {
    class InputContext;
}

// Global state variables for input processing
// LOTUS_EXPORT: read by the headless tests through lotus_test_core.
extern LOTUS_EXPORT std::atomic<fcitx::LotusMode> g_realMode;        ///< Current active input mode
extern std::atomic<bool>                          g_mouseClicked;    ///< Mouse click detection flag
extern std::atomic<bool>                          g_isDeleting;      ///< Deletion in progress flag
extern std::atomic<bool>                          g_stopFlagMonitor; ///< Signal to stop monitor threads
extern std::atomic<unsigned int>                  g_realTextLen;     ///< Current text length
extern std::atomic<int>                           g_mouseSocketFd;   ///< Mouse socket file descriptor

/**
 * @brief Current input mode, read with acquire ordering.
 */
inline fcitx::LotusMode currentMode() {
    return g_realMode.load(std::memory_order_acquire);
}

/**
 * @brief Stores the active input mode with release ordering.
 * @param mode Mode to store.
 */
void setRealMode(fcitx::LotusMode mode);

struct DiffPart {
    std::u32string_view deletedPart;
    std::u32string_view addedPart;
};

/**
 * @brief Builds the socket name of a socket role.
 * @param suffix Socket role, e.g. "kbSocket" or "mouseSocket".
 * @return Socket name truncated to the abstract address limit.
 */
std::string LOTUS_EXPORT buildSocketPath(std::string_view suffix);

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
 * @brief Returns the fcitx5 configuration directory of the current user.
 * @return Path of the fcitx5 configuration directory.
 */
std::string lotusConfigDir();

/**
 * @brief Gets current time in milliseconds.
 * @return Timestamp in milliseconds.
 */
int64_t nowMs();

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
 * @return True for Uinput, Smooth, SuperSmooth and Select.
 */
bool isUinputMode(fcitx::LotusMode mode);

/**
 * @brief Reports whether a program name belongs to a Chromium based browser.
 * @param program Program name reported by fcitx
 * @return True for Chromium-based
 */
bool isChromiumProgram(std::string_view program);

/**
 * @brief Compares two texts and computes diff.
 * @param A First text.
 * @param B Second text.
 * @return Pairs of delete part and add part.
 */
DiffPart compareAndSplitU32(std::u32string_view oldText, std::u32string_view newText);

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
