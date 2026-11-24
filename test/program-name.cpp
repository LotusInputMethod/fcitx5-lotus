// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file program-name.cpp
 * @brief Tests for the program name resolution used as the app-rule key.
 */

#include "lotus-utils.h"
#include "test-utils.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <source_location>
#include <string>

#include <unistd.h>

namespace {
    int  failures = 0;

    void check(const char* name, bool condition, const std::source_location& location = std::source_location::current()) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "program name resolution must follow the app-rule platform rules", location);
            ++failures;
        }
    }

    std::string readSelfComm() {
        std::array<char, 64>                  buffer{};
        std::unique_ptr<FILE, int (*)(FILE*)> file(std::fopen("/proc/self/comm", "r"), &std::fclose);
        if (!file || std::fgets(buffer.data(), static_cast<int>(buffer.size()), file.get()) == nullptr) {
            return {};
        }
        buffer.at(std::strcspn(buffer.data(), "\r\n")) = '\0';
        return buffer.data();
    }
} // namespace

int main() {
    constexpr int     kUnknownPid = 1000000000;
    const std::string selfComm    = readSelfComm();

    check("no pid", programForPid(-1).empty());
    check("pid zero", programForPid(0).empty());
    check("unknown pid", programForPid(kUnknownPid).empty());
    check("own pid resolves to /proc/self/comm", !selfComm.empty() && programForPid(getpid()) == selfComm);

    check("real name wins over the focused program", resolveProgramName("chrome", "code") == "chrome");
    check("wayland falls back to the focused program", resolveProgramName("wayland", "code") == "code");
    check("x11 falls back to the focused program", resolveProgramName("x11", "code") == "code");
    check("shared GNOME context falls back to the focused application", resolveProgramName("gnome-shell", "ghostty") == "ghostty");
    check("empty name falls back to the focused program", resolveProgramName("", "code") == "code");
    check("both unknown", resolveProgramName("wayland", "").empty());

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus program name: all checks passed\n");
    return 0;
}
