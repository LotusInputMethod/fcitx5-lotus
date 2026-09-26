// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file kb-socket-listener.h
 * @brief Shared helper for tests that observe the addon -> uinput server
 *        keyboard socket (kb_socket).
 *
 * Tests bind the abstract socket first, accept the addon's connection and read
 * the KbMsg request the addon emits when a replacement must be delivered by the
 * virtual uinput keyboard (BackSpace stream or Shift+Left selection).
 */

#include "lotus-utils.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

inline void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
    std::cerr << "Step: " << step << '\n';
    std::cerr << "Expected: " << expected << '\n';
    std::cerr << "Actual: " << actual << '\n';
    std::cerr << "Meaning: " << meaning << '\n';
}

class KbSocketListener {
  public:
    KbSocketListener() {
        fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd_ < 0) {
            fail("socket");
            return;
        }
        sockaddr_un address{};
        address.sun_family  = AF_UNIX;
        const auto socketPath = buildSocketPath("kb_socket");
        address.sun_path[0] = '\0';
        std::memcpy(&address.sun_path[1], socketPath.data(), socketPath.size());
        const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + socketPath.size() + 1);
        if (bind(fd_, reinterpret_cast<const sockaddr*>(&address), length) < 0 || listen(fd_, 5) < 0) {
            fail("bind/listen");
        }
    }

    ~KbSocketListener() {
        closeClient();
        if (fd_ >= 0)
            close(fd_);
    }

    KbSocketListener(const KbSocketListener&)            = delete;
    KbSocketListener& operator=(const KbSocketListener&) = delete;
    KbSocketListener(KbSocketListener&&)                 = delete;
    KbSocketListener& operator=(KbSocketListener&&)      = delete;

    bool receive(KbMsg& msg, const char* meaning, const char* requestTimeoutExpected = "request within 5000 ms") {
        int remainingTimeout = kDefaultTimeoutMs;

        while (remainingTimeout > 0) {
            if (client_ < 0) {
                if (fd_ < 0) {
                    reportFailure("wait for replacement socket connection", "valid listener descriptor", "listener descriptor is invalid", meaning);
                    return false;
                }

                pollfd     pfd{fd_, POLLIN, 0};
                const auto pollResult = poll(&pfd, 1, remainingTimeout);
                if (pollResult == 0) {
                    reportFailure("wait for replacement socket connection", "connection request within timeout", "poll timed out", meaning);
                    return false;
                }
                if (pollResult < 0) {
                    if (errno == EINTR)
                        continue;
                    reportFailure("wait for replacement socket connection", "poll succeeds", "poll failed: " + std::string(std::strerror(errno)), meaning);
                    return false;
                }
                if (!(pfd.revents & POLLIN)) {
                    reportFailure("wait for replacement socket connection", "POLLIN revents", "revents=" + std::to_string(pfd.revents), meaning);
                    return false;
                }
                client_ = accept(fd_, nullptr, nullptr);
                if (client_ < 0) {
                    if (errno == EINTR || errno == EAGAIN)
                        continue;
                    reportFailure("accept replacement socket connection", "accept succeeds", "accept failed: " + std::string(std::strerror(errno)), meaning);
                    return false;
                }
            }

            pollfd     pfd{client_, POLLIN | POLLHUP | POLLRDHUP, 0};
            const auto pollResult = poll(&pfd, 1, remainingTimeout);
            if (pollResult == 0) {
                reportFailure("wait for replacement request", requestTimeoutExpected, "poll timed out", meaning);
                return false;
            }
            if (pollResult < 0) {
                if (errno == EINTR)
                    continue;
                reportFailure("wait for replacement request", "poll succeeds", "poll failed: " + std::string(std::strerror(errno)), meaning);
                return false;
            }
            if ((pfd.revents & (POLLHUP | POLLRDHUP)) && !(pfd.revents & POLLIN)) {
                closeClient();
                continue;
            }

            const auto received = recv(client_, &msg, sizeof(msg), 0);
            if (received < 0) {
                if (errno == EINTR)
                    continue;
                reportFailure("receive replacement request", std::to_string(sizeof(msg)) + " bytes", "recv failed: " + std::string(std::strerror(errno)), meaning);
                return false;
            }
            if (received == 0) {
                // Socket closed by remote end
                closeClient();
                continue;
            }
            if (received != static_cast<ssize_t>(sizeof(msg))) {
                reportFailure("receive replacement request", std::to_string(sizeof(msg)) + " bytes", "recv returned " + std::to_string(received) + " bytes", meaning);
                return false;
            }
            return true;
        }

        reportFailure("wait for replacement request", requestTimeoutExpected, "timeout exceeded", meaning);
        return false;
    }

    bool valid() const {
        return fd_ >= 0;
    }

  private:
    static constexpr int kDefaultTimeoutMs = 5000;

    void closeClient() {
        if (client_ >= 0) {
            close(client_);
            client_ = -1;
        }
    }

    void fail(const char* operation) {
        reportFailure(std::string(operation) + " replacement socket", "operation succeeds", std::string(operation) + " failed: " + std::strerror(errno),
                      "the test cannot observe replacement requests");
        close(fd_);
        fd_ = -1;
    }

    int fd_     = -1;
    int client_ = -1;
};
