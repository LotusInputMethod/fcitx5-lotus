// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file executable-path-match.cpp
 * @brief Unit test for isAllowedExecutablePath(): a peer may act as the expected program when its
 *        path matches exactly, or (when a prefix is configured) matches <prefix>*bin/<name>.
 */

#include "common-executable-path.h"
#include "test-utils.h"

#include <source_location>
#include <string>
#include <string_view>

namespace {

    bool expect(std::string_view exe, std::string_view expectedPath, std::string_view prefix, bool want, const std::source_location& location = std::source_location::current()) {
        const bool got = isAllowedExecutablePath(exe, expectedPath, prefix);
        if (got == want) {
            return true;
        }
        reportFailure("isAllowedExecutablePath(\"" + std::string(exe) + "\", \"" + std::string(expectedPath) + "\", \"" + std::string(prefix) + "\")", want ? "true" : "false",
                      got ? "true" : "false", "the executable path policy accepted or rejected a peer incorrectly", location);
        return false;
    }

} // namespace

int main() {
    constexpr std::string_view kFcitx5    = "/usr/bin/fcitx5";
    constexpr std::string_view kServer    = "/usr/bin/fcitx5-lotus-server";
    constexpr std::string_view kNixStore  = "/nix/store/";
    constexpr std::string_view kStorePeer = "/nix/store/abc123-fcitx5-5.1.13/bin/fcitx5";

    // Exact match wins, with or without a configured prefix.
    expect(kFcitx5, kFcitx5, "", true);
    expect(kFcitx5, kFcitx5, kNixStore, true);
    // Store path accepted only when the prefix is configured.
    expect(kStorePeer, kFcitx5, kNixStore, true);
    expect(kStorePeer, kFcitx5, "", false);
    // Wrong directory, wrong program name, and prefix that is not a directory boundary.
    expect("/nix/store/abc123-fcitx5-5.1.13/other/fcitx5", kFcitx5, kNixStore, false);
    expect("/nix/store/abc123-fcitx5-5.1.13/bin/fcitx6", kFcitx5, kNixStore, false);
    expect("/nix/storeabc/bin/fcitx5", kFcitx5, kNixStore, false);
    expect("/usr/libexec/fcitx5", kFcitx5, kNixStore, false);
    // Empty inputs are never allowed.
    expect("", kFcitx5, kNixStore, false);
    expect(kFcitx5, "", kNixStore, false);
    // The server program name has a different basename and still matches its own store path.
    expect("/nix/store/abc-lotus/bin/fcitx5-lotus-server", kServer, kNixStore, true);
    expect("/nix/store/abc-lotus/bin/fcitx5", kServer, kNixStore, false);
    return checkExitCode("executable path match: all checks passed");
}
