/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-executable-path.h"
#include "lotus-fd-guard.h"
#include "lotus-monitor.h"
#include "lotus-utils.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <climits> // IWYU pragma: keep

bool authenticateMouseSocketPeer(int sock, std::string& out_exe_path) {
    struct ucred cred{};
    socklen_t    cred_len = sizeof(cred);

    if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0) {
        LOTUS_ERROR("Failed to get peer credentials: " + std::string(strerror(errno)));
        return false;
    }

    std::array<char, 64> proc_path{};
    snprintf(proc_path.data(), proc_path.size(), "/proc/%d/cmdline", cred.pid);

    FdGuard fd(open(proc_path.data(), O_RDONLY));
    if (!fd.is_valid()) {
        LOTUS_ERROR("Failed to open cmdline: " + std::string(strerror(errno)));
        return false;
    }

    std::array<char, PATH_MAX> exe_path{};
    ssize_t                    bytes_read = read(fd.get(), exe_path.data(), exe_path.size() - 1);

    if (bytes_read <= 0) {
        LOTUS_ERROR("Failed to read cmdline: " + std::string(strerror(errno)));
        return false;
    }

    out_exe_path = exe_path.data();

    return isAllowedExecutablePath(exe_path.data(), LOTUS_SERVER_EXECUTABLE, LOTUS_ALT_EXECUTABLE_PREFIX);
}
