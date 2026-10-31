/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-kb-sender.h"

#include "lotus-log.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>

KbSender::KbSender(std::string socketPath) : socketPath_(std::move(socketPath)) {}

KbSender::~KbSender() {
    close();
}

bool KbSender::connected() const {
    std::scoped_lock lock(mutex_);
    return fd_.isValid();
}

bool KbSender::ensureConnected() {
    std::scoped_lock lock(mutex_);
    if (fd_.isValid()) {
        return true;
    }
    return connectLocked();
}

bool KbSender::send(const KbMsg& msg) {
    std::scoped_lock lock(mutex_);
    if (!fd_.isValid()) {
        connectLocked();
    }
    if (!fd_.isValid()) {
        LOTUS_ERROR("Cannot send key request since cannot connect to uinput server");
        return false;
    }
    const ssize_t written = ::send(fd_.get(), &msg, sizeof(msg), MSG_NOSIGNAL);
    if (std::cmp_equal(written, sizeof(msg))) {
        return true;
    }
    LOTUS_WARN("Failed to send key request: " + std::string(std::strerror(errno)));
    fd_.reset();
    if (!connectLocked()) {
        return false;
    }
    const ssize_t retried = ::send(fd_.get(), &msg, sizeof(msg), MSG_NOSIGNAL);
    if (std::cmp_equal(retried, sizeof(msg))) {
        LOTUS_INFO("Reconnected to uinput server successfully");
        return true;
    }
    LOTUS_WARN("Failed to send key request after reconnect: " + std::string(std::strerror(errno)));
    return false;
}

void KbSender::close() {
    std::scoped_lock lock(mutex_);
    fd_.reset();
}

bool KbSender::connectLocked() {
    sockaddr_un address{};
    address.sun_family    = AF_UNIX;
    address.sun_path[0]   = '\0';
    const auto socketPath = socketPath_;
    std::memcpy(&address.sun_path[1], socketPath.data(), socketPath.size());
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + socketPath.size() + 1);

    FdGuard    candidate(socket(AF_UNIX, SOCK_SEQPACKET, 0));
    if (!candidate.isValid()) {
        LOTUS_ERROR("Failed to create keyboard socket: " + std::string(std::strerror(errno)));
        return false;
    }
    if (connect(candidate.get(), reinterpret_cast<const sockaddr*>(&address), length) != 0) {
        LOTUS_ERROR("Failed to connect to socket: " + std::string(std::strerror(errno)));
        return false;
    }
    fd_ = std::move(candidate);
    return true;
}
