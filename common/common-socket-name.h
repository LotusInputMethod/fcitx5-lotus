/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoàng Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/*
 * Header-only: shared by the addon and the server, two targets that must not link each other.
 */

#ifndef _COMMON_SOCKET_NAME_H_
#define _COMMON_SOCKET_NAME_H_

#include <cstdlib>
#include <string>
#include <string_view>

/**
 * @brief Environment variable that overrides the namespace segment of the socket names.
 */
inline constexpr const char* LOTUS_SOCKET_NAMESPACE_ENV = "LOTUS_SOCKET_NAMESPACE";

/**
 * @brief Resolves the socket namespace segment.
 * @return Value of LOTUS_SOCKET_NAMESPACE_ENV when set and non-empty, an empty string otherwise.
 */
inline std::string lotusSocketNamespace() {
    const char* override_value = std::getenv(LOTUS_SOCKET_NAMESPACE_ENV);
    if (override_value != nullptr && *override_value != '\0') {
        return override_value;
    }
    return {};
}

/**
 * @brief Builds the abstract socket name for a socket role.
 * @param suffix Socket role, e.g. "kbSocket" or "mouseSocket".
 * @return Name in the form "lotussocket[-<namespace>]-<suffix>".
 */
inline std::string lotusSocketName(std::string_view suffix) {
    std::string name;
    name.reserve(suffix.size() + 12);
    name += "lotussocket-";
    const std::string namespaceSegment = lotusSocketNamespace();
    if (!namespaceSegment.empty()) {
        name += namespaceSegment;
        name += '-';
    }
    name += suffix;
    return name;
}

#endif // _COMMON_SOCKET_NAME_H_
