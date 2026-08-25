/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-monitor.h"
#include "lotus-utils.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <limits.h>

std::thread mouse_thread = std::thread();

static bool authenticateMouseSocketPeer(int sock, std::string& out_exe_path) {
    struct ucred cred{};
    socklen_t    cred_len = sizeof(cred);

    if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0) {
        LOTUS_ERROR("Failed to get peer credentials: " + std::string(strerror(errno)));
        return false;
    }

    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/cmdline", cred.pid);

    int fd = open(proc_path, O_RDONLY);
    if (fd < 0) {
        LOTUS_ERROR("Failed to open cmdline: " + std::string(strerror(errno)));
        return false;
    }

    char exe_path[PATH_MAX] = {0};
    ssize_t bytes_read = read(fd, exe_path, sizeof(exe_path) - 1);
    close(fd);

    if (bytes_read <= 0) {
        LOTUS_ERROR("Failed to read cmdline: " + std::string(strerror(errno)));
        return false;
    }

    out_exe_path = exe_path;

    return strcmp(exe_path, "/usr/bin/fcitx5-lotus-server") == 0;
}

void        mousePressResetThread() {
    const std::string mouse_socket_path = buildSocketPath("mouse_socket");
    LOTUS_INFO("Mouse press reset thread started.");

    while (!stop_flag_monitor.load(std::memory_order_acquire)) {
        int sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
        if (sock < 0) {
            LOTUS_ERROR("Failed to create socket: " + std::string(strerror(errno)));
            sleep(1);
            continue;
        }

        struct sockaddr_un addr{};
        addr.sun_family  = AF_UNIX;
        addr.sun_path[0] = '\0';
        memcpy(&addr.sun_path[1], mouse_socket_path.c_str(), mouse_socket_path.length());
        socklen_t len = offsetof(struct sockaddr_un, sun_path) + mouse_socket_path.length() + 1;

        if (connect(sock, (struct sockaddr*)&addr, len) < 0) {
            LOTUS_ERROR("Failed to connect to socket: " + std::string(strerror(errno)));
            close(sock);
            sleep(1);
            continue;
        }
        LOTUS_INFO("Mouse socket connected.");

        std::string peer_exe_path;
        if (!authenticateMouseSocketPeer(sock, peer_exe_path)) {
            LOTUS_WARN("Unauthorized connection attempt from: " + peer_exe_path);
            close(sock);
            sleep(1);
            continue;
        }
        
        mouse_socket_fd.store(sock, std::memory_order_release);

        struct pollfd pfd{};
        pfd.fd     = sock;
        pfd.events = POLLIN;

        while (!stop_flag_monitor.load(std::memory_order_acquire)) {
            int ret = poll(&pfd, 1, -1);

            if (ret > 0 && ((pfd.revents & POLLIN) != 0)) {
                char    buf[16];
                ssize_t n = recv(sock, buf, sizeof(buf), 0);

                if (n <= 0) {
                    LOTUS_ERROR("Mouse socket recv error: " + std::string(strerror(errno)));
                    break;
                }

                if (n >= 1 && buf[0] == 'C') {
                    LOTUS_DEBUG("Mouse click detected from server. Resetting engine.");
                    needEngineReset.store(true, std::memory_order_release);
                    g_mouse_clicked.store(true, std::memory_order_release);
                }
                else {
                    LOTUS_WARN("Unexpected message received from mouse socket: " + std::string(buf, n));
                }

            } else if (ret < 0 && errno != EINTR) {
                LOTUS_ERROR("Mouse socket poll error: " + std::string(strerror(errno)));
                break;
            }
        }
        mouse_socket_fd.store(-1, std::memory_order_release);
        close(sock);
    }
}

void startMouseReset() {
    mouse_thread = std::thread(mousePressResetThread);
}
