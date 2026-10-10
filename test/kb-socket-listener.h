// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file kb-socket-listener.h
 * @brief Shared helper for tests that observe the addon -> uinput server
 *        keyboard socket (kb_socket).
 *
 * Tests bind the abstract socket first, accept the addon's connection and read
 * the KeyMsg stream the addon emits when a replacement is paced by KeyPacer
 * (BackSpace taps or Shift+Left selection with legacy tail).
 */

#include "lotus-protocol.h"
#include "lotus-utils.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

/**
 * @brief One received KeyMsg with its steady-clock arrival time.
 */
struct StampedKey {
    fcitx::KeyMsg                         msg{};
    std::chrono::steady_clock::time_point when{};
};

class KbSocketListener {
  public:
    KbSocketListener() : fd_(socket(AF_UNIX, SOCK_SEQPACKET, 0)) {

        if (fd_ < 0) {
            fail("socket");
            return;
        }
        sockaddr_un address{};
        address.sun_family    = AF_UNIX;
        const auto socketPath = buildSocketPath("kb_socket");
        address.sun_path[0]   = '\0';
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

    /**
     * @brief Collects KeyMsgs until the stream goes quiet.
     *
     * Returns when no datagram arrives for quietMs (job fully paced) or when
     * totalMs elapses. The first items of a job may already sit in the socket
     * buffer (KeyPacer bursts synchronously inside push()).
     *
     * @param out Received items with arrival timestamps.
     * @param meaning Failure context for diagnostics.
     * @param quietMs Returns after this long without new data.
     * @param totalMs Hard deadline for the whole drain.
     * @return True if at least one KeyMsg arrived before the deadline.
     */
    bool receiveAll(std::vector<StampedKey>& out, const char* meaning, int quietMs = 400, int totalMs = 8000) {
        out.clear();
        if (fd_ < 0) {
            reportFailure("wait for replacement socket connection", "valid listener descriptor", "listener descriptor is invalid", meaning);
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(totalMs);
        while (true) {
            if (client_ < 0 && !acceptClient(deadline, meaning))
                return !out.empty();
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                break;
            const int  waitMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            pollfd     pfd{client_, POLLIN | POLLHUP | POLLRDHUP, 0};
            const auto pollResult = poll(&pfd, 1, std::min(waitMs, quietMs));
            if (pollResult < 0) {
                if (errno == EINTR)
                    continue;
                reportFailure("wait for key stream", "poll succeeds", "poll failed: " + std::string(std::strerror(errno)), meaning);
                return !out.empty();
            }
            if (pollResult == 0) {
                if (!out.empty())
                    return true; // quiet: job fully paced
                continue;        // no data yet, keep waiting for deadline
            }
            if ((static_cast<unsigned>(pfd.revents) & (static_cast<unsigned>(POLLHUP) | static_cast<unsigned>(POLLRDHUP))) != 0 &&
                (static_cast<unsigned>(pfd.revents) & static_cast<unsigned>(POLLIN)) == 0) {
                closeClient();
                continue;
            }
            fcitx::KeyMsg msg{};
            const auto    received = recv(client_, &msg, sizeof(msg), 0);
            if (received < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                reportFailure("receive key stream", std::to_string(sizeof(msg)) + " bytes", "recv failed: " + std::string(std::strerror(errno)), meaning);
                return !out.empty();
            }
            if (received == 0) {
                closeClient();
                continue;
            }
            if (received != static_cast<ssize_t>(sizeof(msg))) {
                reportFailure("receive key stream", std::to_string(sizeof(msg)) + " bytes", "recv returned " + std::to_string(received) + " bytes", meaning);
                return false;
            }
            out.push_back({msg, std::chrono::steady_clock::now()});
        }
        return !out.empty();
    }

    [[nodiscard]] bool valid() const {
        return fd_ >= 0;
    }

  private:
    bool acceptClient(std::chrono::steady_clock::time_point deadline, const char* meaning) {
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                reportFailure("wait for replacement socket connection", "connection before deadline", "timed out", meaning);
                return false;
            }
            const int  waitMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            pollfd     pfd{fd_, POLLIN, 0};
            const auto pollResult = poll(&pfd, 1, waitMs);
            if (pollResult == 0) {
                reportFailure("wait for replacement socket connection", "connection before deadline", "timed out", meaning);
                return false;
            }
            if (pollResult < 0) {
                if (errno == EINTR)
                    continue;
                reportFailure("wait for replacement socket connection", "poll succeeds", "poll failed: " + std::string(std::strerror(errno)), meaning);
                return false;
            }
            client_ = accept(fd_, nullptr, nullptr);
            if (client_ < 0) {
                if (errno == EINTR)
                    continue;
                reportFailure("accept replacement socket connection", "accept succeeds", "accept failed: " + std::string(std::strerror(errno)), meaning);
                return false;
            }
            return true;
        }
    }

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
