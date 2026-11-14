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

#include <cstring>
#include <string>
#include <unistd.h>

namespace lotus::apphook {

    bool socketAddress(sockaddr_un& address, socklen_t& size) {
        const std::string name = "lotus-apphook-v3-" + std::to_string(getuid());
        if (name.size() + 1 > sizeof(address.sun_path)) {
            return false;
        }
        address             = {};
        address.sun_family  = AF_UNIX;
        address.sun_path[0] = '\0';
        std::memcpy(address.sun_path + 1, name.data(), name.size());
        size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
        return true;
    }

} // namespace lotus::apphook
