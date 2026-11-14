// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file kb-socket-listener.h
 * @brief Shared helper for tests that observe the addon -> uinput server
 *        keyboard socket (kbSocket).
 *
 * Tests bind the abstract socket first, accept the addon's connection and read
 * the KbMsg request the addon emits when a replacement must be delivered by the
 * virtual uinput keyboard (BackSpace stream or Shift+Left selection).
 */

#include "common-fd-guard.h"
#include "common-protocol.h"
#include "lotus-utils.h"
#include "test-util.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

class KbSocketListener {
  public:
    KbSocketListener() : fd_(socket(AF_UNIX, SOCK_SEQPACKET, 0)) {

        if (!fd_.isValid()) {
            fail("socket");
            return;
        }
        sockaddr_un address{};
        address.sun_family    = AF_UNIX;
        const auto socketPath = buildSocketPath("kbSocket");
        address.sun_path[0]   = '\0';
        std::memcpy(&address.sun_path[1], socketPath.data(), socketPath.size());
        const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + socketPath.size() + 1);
        if (bind(fd_.get(), reinterpret_cast<const sockaddr*>(&address), length) < 0 || listen(fd_.get(), 5) < 0) {
            fail("bind/listen");
        }
    }

    ~KbSocketListener()                                  = default;
    KbSocketListener(const KbSocketListener&)            = delete;
    KbSocketListener& operator=(const KbSocketListener&) = delete;
    KbSocketListener(KbSocketListener&&)                 = delete;
    KbSocketListener& operator=(KbSocketListener&&)      = delete;

    bool              receive(KbMsg& msg, std::string_view meaning, std::string_view requestTimeoutExpected = "request within 5000 ms") {
        int remainingTimeout = kDefaultTimeoutMs;

        while (remainingTimeout > 0) {
            if (!client_.isValid()) {
                if (!fd_.isValid()) {
                    reportFailure("wait for replacement socket connection", "valid listener descriptor", "listener descriptor is invalid", meaning);
                    return false;
                }

                pollfd     pfd{.fd = fd_.get(), .events = POLLIN, .revents = 0};
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
                if ((static_cast<unsigned>(pfd.revents) & POLLIN) == 0) {
                    reportFailure("wait for replacement socket connection", "POLLIN revents", "revents=" + std::to_string(pfd.revents), meaning);
                    return false;
                }
                client_ = FdGuard(accept(fd_.get(), nullptr, nullptr));
                if (!client_.isValid()) {
                    if (errno == EINTR || errno == EAGAIN)
                        continue;
                    reportFailure("accept replacement socket connection", "accept succeeds", "accept failed: " + std::string(std::strerror(errno)), meaning);
                    return false;
                }
            }

            pollfd     pfd{.fd = client_.get(), .events = POLLIN | POLLHUP | POLLRDHUP, .revents = 0};
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
            if ((static_cast<unsigned>(pfd.revents) & (static_cast<unsigned>(POLLHUP) | static_cast<unsigned>(POLLRDHUP))) != 0 &&
                (static_cast<unsigned>(pfd.revents) & static_cast<unsigned>(POLLIN)) == 0) {
                closeClient();
                continue;
            }

            const auto received = recv(client_.get(), &msg, sizeof(msg), 0);
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
            if (std::cmp_not_equal(received, sizeof(msg))) {
                reportFailure("receive replacement request", std::to_string(sizeof(msg)) + " bytes", "recv returned " + std::to_string(received) + " bytes", meaning);
                return false;
            }
            return true;
        }

        reportFailure("wait for replacement request", requestTimeoutExpected, "timeout exceeded", meaning);
        return false;
    }

    [[nodiscard]] bool valid() const {
        return fd_.isValid();
    }

    /**
     * @brief Reads a request that is already queued, without waiting.
     *
     * The addon writes a request synchronously inside the key event, so a test that must prove "no
     * request happened" can check right after the key instead of waiting for a timeout.
     * @param msg Request to fill.
     * @return True when a request was already pending.
     */
    bool tryReceive(KbMsg& msg) {
        if (!fd_.isValid()) {
            return false;
        }
        if (!client_.isValid()) {
            pollfd     pending{.fd = fd_.get(), .events = POLLIN, .revents = 0};
            const auto accepted = poll(&pending, 1, 0);
            if (accepted <= 0 || (static_cast<unsigned>(pending.revents) & static_cast<unsigned>(POLLIN)) == 0) {
                return false;
            }
            client_ = FdGuard(accept(fd_.get(), nullptr, nullptr));
            if (!client_.isValid()) {
                return false;
            }
        }
        pollfd     ready{.fd = client_.get(), .events = POLLIN | POLLHUP | POLLRDHUP, .revents = 0};
        const auto polled = poll(&ready, 1, 0);
        if (polled <= 0 || (static_cast<unsigned>(ready.revents) & static_cast<unsigned>(POLLIN)) == 0) {
            return false;
        }
        const auto received = recv(client_.get(), &msg, sizeof(msg), 0);
        return std::cmp_equal(received, sizeof(msg));
    }

  private:
    static constexpr int kDefaultTimeoutMs = 5000;

    void                 closeClient() {
        client_.reset();
    }

    void fail(std::string_view operation) {
        reportFailure(std::string(operation) + " replacement socket", "operation succeeds", std::string(operation) + " failed: " + std::strerror(errno),
                      "the test cannot observe replacement requests");
        fd_.reset();
    }

    FdGuard fd_;
    FdGuard client_;
};
