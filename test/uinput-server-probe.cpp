// SPDX-License-Identifier: GPL-3.0-or-later
//
// Switching to a uinput mode must know whether the uinput server is running. The probe reuses the
// keyboard socket: no listener means no server, a listening socket means the server is up.
#include "kb-socket-listener.h"
#include "lotus-kb-sender.h"
#include "lotus-utils.h"
#include "test-input-context.h"

#include <cstdio>

namespace {
    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
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

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus uinput server probe: all checks passed\n");
    return 0;
}
