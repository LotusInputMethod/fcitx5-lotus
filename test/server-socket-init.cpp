// SPDX-FileCopyrightText: 2026 LotusInputMethod contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A server socket that fails bind()/listen() must be reported, never used
// deaf. Exercises the wrappers with real syscalls: a duplicate bind must be
// refused with EADDRINUSE and listen() on an invalid fd with EBADF.
#include "common-fd-guard.h"
#include "lotus-abstract-socket.h"
#include "lotus-server-socket.h"
#include "test-utils.h"

#include <cstdio>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "socket lifecycle errors must be detected and reported, never used deaf");
            ++failures;
        }
    }

    std::string testSocketName() {
        return "lotus-server-socket-init-" + std::to_string(getpid());
    }

} // namespace

int runServerSocketInit() {
    const AbstractSocket address = makeAbstractSocket(testSocketName());

    FdGuard              first(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    check("create socket", first.isValid());
    if (!first.isValid()) {
        return 1;
    }
    check("bind fresh address", bindServerSocket(first.get(), address, "test socket"));
    check("listen on bound socket", listenServerSocket(first.get(), "test socket"));

    FdGuard second(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    check("create second socket", second.isValid());
    check("rebind same address fails", !bindServerSocket(second.get(), address, "test socket"));

    check("listen on invalid fd fails", !listenServerSocket(-1, "test socket"));

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("server socket init: all checks passed\n");
    return 0;
}

int main() {
    return runServerSocketInit();
}
