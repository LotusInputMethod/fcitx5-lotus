/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-executable-path.h"

bool isAllowedExecutablePath(std::string_view exePath, std::string_view expectedPath, std::string_view altPrefix) {
    if (exePath.empty() || expectedPath.empty()) {
        return false;
    }
    if (exePath == expectedPath) {
        return true;
    }
    if (altPrefix.empty() || exePath.size() <= altPrefix.size() || !exePath.starts_with(altPrefix)) {
        return false;
    }

    constexpr std::string_view kBinDir = "/bin/";
    const auto                 slash   = expectedPath.rfind('/');
    const std::string_view     name    = expectedPath.substr(slash == std::string_view::npos ? 0 : slash + 1);
    if (name.empty() || exePath.size() < kBinDir.size() + name.size()) {
        return false;
    }

    const auto namePos = exePath.size() - name.size();
    return exePath.compare(namePos, name.size(), name) == 0 && exePath.compare(namePos - kBinDir.size(), kBinDir.size(), kBinDir) == 0;
}
