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
//
void mousePressResetThread() {
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

                struct ucred cred{};
                socklen_t    len                = sizeof(struct ucred);
                char         exe_path[PATH_MAX] = {0};
                if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
                    char path[64];
                    snprintf(path, sizeof(path), "/proc/%d/cmdline", cred.pid);
                    int fd = open(path, O_RDONLY);
                    if (fd >= 0) {
                        if (read(fd, exe_path, sizeof(exe_path) - 1) < 0) {
                            LOTUS_ERROR("Failed to read cmdline: " + std::string(strerror(errno)));
                        }
                        close(fd);
                    }
                }

                if (strcmp(exe_path, "/usr/bin/fcitx5-lotus-server") == 0) {
                    LOTUS_DEBUG("Mouse click detected from server. Resetting engine.");
                    needEngineReset.store(true, std::memory_order_release);
                    g_mouse_clicked.store(true, std::memory_order_release);
                } else {
                    LOTUS_WARN("Unauthorized connection attempt from: " + std::string(exe_path));
                }
            } else if (ret < 0 && errno != EINTR) {
                LOTUS_ERROR("Mouse socket poll error: " + std::string(strerror(errno)));
                break;
            }
        }
        close(sock);
        mouse_socket_fd.store(-1, std::memory_order_release);
    }
}

void startMouseReset() {
    mouse_thread = std::thread(mousePressResetThread);
}
