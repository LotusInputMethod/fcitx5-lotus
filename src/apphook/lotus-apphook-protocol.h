/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-protocol.h
 * @brief Defines messages and the shared abstract Unix socket address.
 */

#ifndef _SRC_LOTUS_APPHOOK_PROTOCOL_H_
#define _SRC_LOTUS_APPHOOK_PROTOCOL_H_

#include "lotus-apphook-keys.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <sys/socket.h>
#include <sys/un.h>

namespace lotus::apphook {

    constexpr uint32_t kMagic        = 0x4c485031; // LHP1
    constexpr uint16_t kVersion      = 1;
    constexpr size_t   kMaxOps       = 128;
    constexpr size_t   kMaxTaps      = 4096;
    constexpr size_t   kMaxTasks     = 64;
    constexpr size_t   kMaxPeers     = 128;
    constexpr uint32_t kReadyFocused = 1U; ///< Focus snapshot carried by Ready.

    /** @brief Commands sent from the Fcitx5 addon to an application hook. */
    enum class Command : uint16_t {
        Emit   = 1,
        Cancel = 2
    };

    /** @brief Messages sent from an application hook to the Fcitx5 addon. */
    enum class ResponseKind : uint16_t {
        Ready    = 1,
        Complete = 2,
        Focus    = 3
    };

    /** @brief Versioned keyboard request with optional operations. */
    struct Request {
        uint32_t                          magic     = kMagic;
        uint16_t                          version   = kVersion;
        Command                           type      = Command::Emit;
        uint64_t                          requestId = 0;
        uint32_t                          count     = 0;
        uint32_t                          reserved  = 0;
        std::array<KeyOperation, kMaxOps> ops{};
    };

    /** @brief Hook readiness with initial focus, subsequent focus notifications or request completion. */
    struct Response {
        uint32_t     magic     = kMagic;
        uint16_t     version   = kVersion;
        ResponseKind type      = ResponseKind::Ready;
        int32_t      status    = 0;
        uint32_t     reserved  = 0; ///< kReadyFocused is set for a focused Ready response.
        uint64_t     requestId = 0;
    };

    constexpr size_t kRequestHeaderSize = offsetof(Request, ops);
    static_assert(kRequestHeaderSize == 24);
    static_assert(sizeof(Response) == 24);
    static_assert(sizeof(KeyOperation) == 12);

    /**
     * @brief Builds one UID-scoped abstract socket address in the current network namespace.
     * @param address Output Unix address, including an initial NUL byte.
     * @param size Output sockaddr length without a trailing NUL terminator.
     * @return True when the name fits in sun_path.
     */
    bool socketAddress(sockaddr_un& address, socklen_t& size);

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_PROTOCOL_H_
