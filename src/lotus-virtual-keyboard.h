/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-virtual-keyboard.h
 * @brief Thin synchronous sender for KeyMsg over the kb_socket.
 *
 * Single responsibility: own the socket fd and send one KeyMsg at a time.
 * No sleep, no thread, no queue — pacing lives in KeyPacer.
 */

#ifndef _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_
#define _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_

#include <cstdint>
#include <mutex>
#include <string>

namespace fcitx {

class VirtualKeyboard {
  public:
    VirtualKeyboard()  = default;
    ~VirtualKeyboard();

    VirtualKeyboard(const VirtualKeyboard&)            = delete;
    VirtualKeyboard& operator=(const VirtualKeyboard&) = delete;
    VirtualKeyboard(VirtualKeyboard&&)                 = delete;
    VirtualKeyboard& operator=(VirtualKeyboard&&)      = delete;

    /**
     * @brief Overrides the socket path (tests only; default is kb_socket).
     */
    void setSocketPath(std::string path);

    /**
     * @brief Lazily connects to the kb_socket (abstract Unix socket).
     * @return True if a connected fd is held.
     */
    bool ensureConnected();

    /**
     * @brief Sends one KeyMsg (no reconnect, no sleep).
     * @return True if the full message was sent.
     */
    bool send(uint16_t code, uint8_t pressed);

    /**
     * @brief Closes the held fd, if any.
     */
    void close();

  private:
    int        fd_{-1};
    std::string path_; ///< override for tests; empty = buildSocketPath("kb_socket")
    std::mutex m_;
};

} // namespace fcitx

#endif // _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_
