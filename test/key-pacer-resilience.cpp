// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file key-pacer-resilience.cpp
 * @brief Pacer resilience: Shift release on stop() and reconnect after the
 *        kb_socket peer dies. Uses a private abstract socket path, so it runs
 *        even while a live fcitx5-lotus-server holds the real kb_socket.
 */

#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

    // Abstract socket name WITHOUT leading NUL (helpers add sun_path[0] = '\0',
    // same convention as buildSocketPath). Private to this binary, so the live
    // server's kb_socket never collides.
    const std::string kPath("lotus-test-resilience-kb");

    void              buildAddr(sockaddr_un& addr, socklen_t& len) {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family  = AF_UNIX;
        addr.sun_path[0] = '\0';
        memcpy(&addr.sun_path[1], kPath.data(), kPath.size());
        len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + kPath.size() + 1);
    }

    int bindListener() {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
        if (fd < 0)
            return -1;
        sockaddr_un addr{};
        socklen_t   len = 0;
        buildAddr(addr, len);
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), len) < 0 || listen(fd, 5) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    int acceptOne(int listenFd, int timeoutMs) {
        pollfd pfd{listenFd, POLLIN, 0};
        if (poll(&pfd, 1, timeoutMs) <= 0)
            return -1;
        return accept(listenFd, nullptr, nullptr);
    }

    // Receives exactly `want` KeyMsgs (with an overall deadline).
    bool recvExact(int clientFd, size_t want, std::vector<fcitx::KeyMsg>& out, int totalMs = 3000) {
        out.clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(totalMs);
        while (out.size() < want) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return false;
            const int wait = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            pollfd    pfd{clientFd, POLLIN, 0};
            if (poll(&pfd, 1, wait) <= 0)
                return false;
            fcitx::KeyMsg msg{};
            if (recv(clientFd, &msg, sizeof(msg), 0) != static_cast<ssize_t>(sizeof(msg)))
                return false;
            out.push_back(msg);
        }
        return true;
    }

#define CHECK(cond, what)                                                                                                                                                          \
    do {                                                                                                                                                                           \
        if (!(cond)) {                                                                                                                                                             \
            std::cerr << "FAIL: " << what << " (line " << __LINE__ << ")\n";                                                                                                       \
            return 1;                                                                                                                                                              \
        }                                                                                                                                                                          \
    } while (0)

} // namespace

int main() {
    using namespace std::chrono_literals;
    configureTestPaths("fcitx5-lotus-key-pacer-resilience");
    TestInstance testInstance;
    fcitx::KeyPacer::instance().init(&testInstance.instance);
    fcitx::KeyPacer::instance().setSocketPathForTest(kPath);

    // Scenario A: stop() mid-selection must release Shift (KEY_LEFTSHIFT=42).
    {
        const int listenFd = bindListener();
        CHECK(listenFd >= 0, "scenario A bind");
        fcitx::KeyPacer::instance().push(fcitx::buildSelectJob(2, 0ms, 60000ms, 60000ms));
        const int clientFd = acceptOne(listenFd, 3000);
        CHECK(clientFd >= 0, "scenario A accept");
        std::vector<fcitx::KeyMsg> burst;
        CHECK(recvExact(clientFd, 1, burst) && burst[0].code == KEY_LEFTSHIFT && burst[0].pressed == 1, "scenario A Shift-down burst");
        // Selection in flight (Shift logically down, rest queued behind a long sleep).
        fcitx::KeyPacer::instance().stop();
        std::vector<fcitx::KeyMsg> tail;
        CHECK(recvExact(clientFd, 1, tail) && tail[0].code == KEY_LEFTSHIFT && tail[0].pressed == 0, "scenario A stop() releases Shift-up");
        close(clientFd);
        close(listenFd);
    }

    // Scenario B: peer death must trigger reconnect, not silent drops.
    {
        int listenFd = bindListener();
        CHECK(listenFd >= 0, "scenario B bind 1");
        fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);
        fcitx::KeyPacer::instance().push(fcitx::buildBackspaceJob(1, 0ms, 1ms, 1ms));
        int clientFd = acceptOne(listenFd, 3000);
        CHECK(clientFd >= 0, "scenario B accept 1");
        std::vector<fcitx::KeyMsg> first;
        CHECK(recvExact(clientFd, 2, first), "scenario B first tap");
        // Kill the peer entirely, then rebind the same path (like a server restart).
        close(clientFd);
        close(listenFd);
        listenFd = bindListener();
        CHECK(listenFd >= 0, "scenario B rebind");
        fcitx::KeyPacer::instance().push(fcitx::buildBackspaceJob(1, 0ms, 1ms, 1ms));
        clientFd = acceptOne(listenFd, 3000);
        CHECK(clientFd >= 0, "scenario B accept 2 (reconnected)");
        std::vector<fcitx::KeyMsg> second;
        CHECK(recvExact(clientFd, 2, second) && second[0].code == KEY_BACKSPACE && second[0].pressed == 1, "scenario B tap after reconnect");
        close(clientFd);
        close(listenFd);
    }

    std::cout << "key-pacer-resilience: all checks passed\n";
    return 0;
}
