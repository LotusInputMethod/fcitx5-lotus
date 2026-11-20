/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-protocol.cpp
 * @brief Builds the per-UID abstract Unix socket address.
 */

#include "lotus-apphook-protocol.h"

#include <algorithm>
#include <span>
#include <string>
#include <unistd.h>

namespace lotus::apphook {

    bool socketAddress(sockaddr_un& address, socklen_t& size) {
        const std::string name = "lotus-apphook-v1-" + std::to_string(getuid());
        if (name.size() + 1 > sizeof(address.sun_path)) {
            return false;
        }
        address            = {};
        address.sun_family = AF_UNIX;
        // Abstract namespace: a leading NUL followed by the unterminated name.
        const std::span path(address.sun_path);
        path[0] = '\0';
        std::ranges::copy(name, path.subspan(1).begin());
        size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
        return true;
    }

} // namespace lotus::apphook
