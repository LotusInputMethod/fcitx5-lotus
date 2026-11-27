// SPDX-License-Identifier: GPL-3.0-or-later
//
// Switching to a uinput mode must know whether the uinput server is running. The probe reuses the
// keyboard socket: no listener means no server, a listening socket means the server is up.
#include "kb-socket-listener.h"
#include "lotus-kb-sender.h"
#include "lotus-utils.h"
#include "test-input-context.h"
#include "test-utils.h"

#include <cstdio>
#include <source_location>

namespace {
    void check(const char* name, bool condition, const std::source_location& location = std::source_location::current()) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "the uinput probe must reflect whether the keyboard server is reachable", location);
        }
    }
} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-uinput-server-probe");
    KbSender sender(buildSocketPath("kbSocket"));

    check("no server is detected before the socket exists", !sender.ensureConnected());

    KbSocketListener listener;
    if (!listener.valid())
        return 1;
    check("a listening keyboard socket is detected as a running server", sender.ensureConnected());

    sender.close();
    check("a dropped connection is re-established", sender.ensureConnected());

    return checkExitCode("lotus uinput server probe: all checks passed");
}
