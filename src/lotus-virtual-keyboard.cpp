/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-virtual-keyboard.h"
#include "lotus-protocol.h"
#include "lotus-utils.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fcitx {

VirtualKeyboard::~VirtualKeyboard() {
    close();
}

void VirtualKeyboard::setSocketPath(std::string path) {
    std::lock_guard<std::mutex> lock(m_);
    if (path_ != path) {
        path_ = std::move(path);
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
}

bool VirtualKeyboard::ensureConnected() {
    std::lock_guard<std::mutex> lock(m_);
    if (fd_ >= 0)
        return true;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr{};
    addr.sun_family  = AF_UNIX;
    const auto path  = path_.empty() ? buildSocketPath("kb_socket") : path_;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], path.data(), path.size());
    auto len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + path.size() + 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        fd_ = fd;
        return true;
    }
    ::close(fd);
    return false;
}

bool VirtualKeyboard::send(uint16_t code, uint8_t pressed) {
    KeyMsg msg{};
    msg.code    = code;
    msg.pressed = pressed;
    std::lock_guard<std::mutex> lock(m_);
    if (fd_ < 0)
        return false;
    const ssize_t n = ::send(fd_, &msg, sizeof(msg), MSG_NOSIGNAL);
    if (n == static_cast<ssize_t>(sizeof(msg)))
        return true;
    // Dead peer (server restart): drop the fd so the caller's
    // ensureConnected()+retry actually reconnects instead of reusing it.
    // Dead peer (server restart): drop the fd so the caller's
    // ensureConnected()+retry actually reconnects instead of reusing it.
    const int err = errno;
    if (err == EPIPE || err == ECONNRESET || err == ENOTCONN || err == EBADF) {
        ::close(fd_);
        fd_ = -1;
    }
    return false;
}

void VirtualKeyboard::close() {
    std::lock_guard<std::mutex> lock(m_);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace fcitx
