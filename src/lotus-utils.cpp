/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-utils.h"
#include "common-socket-name.h"
#include "lotus-log.h"

#include <cstddef>
#include <cstring>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <pwd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>

std::atomic<fcitx::LotusMode> g_realMode{fcitx::LotusMode::Smooth};
std::atomic<bool>             g_mouseClicked{false};
std::atomic<bool>             g_isDeleting{false};
std::atomic<bool>             g_stopFlagMonitor{false};
std::atomic<unsigned int>     g_realTextLen{0};
std::atomic<int>              g_mouseSocketFd{-1};

namespace fcitx {
    FCITX_DEFINE_LOG_CATEGORY(lotusLog, "lotus", fcitx::LogLevel::NoLog);
} // namespace fcitx

void setRealMode(fcitx::LotusMode mode) {
    g_realMode.store(mode, std::memory_order_release);
}

std::string buildSocketPath(std::string_view base_path_suffix) {
    struct passwd  pwd{};
    struct passwd* result  = nullptr;
    long           bufSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufSize == -1) {
        bufSize = 16384;
    }
    std::vector<char> buf(bufSize);
    std::string       username;
    int               res = getpwuid_r(getuid(), &pwd, buf.data(), bufSize, &result);
    if (res == 0 && result != nullptr) {
        username = result->pw_name;
    } else {
        username = "unknown";
    }
    std::string path;
    path.reserve(32);
    path += "lotussocket-";
    path += lotusSocketNamespace(username);
    path += '-';
    path += base_path_suffix;
    const size_t maxSocketPathLength = UNIX_PATH_MAX - 1;
    path.resize(std::min(path.length(), maxSocketPathLength));
    return path;
}

int makeAbstractUnixSocket(std::string_view name, struct sockaddr_un& addr, socklen_t& len) {
    const int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }
    addr.sun_family  = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], name.data(), name.size());
    len = offsetof(struct sockaddr_un, sun_path) + name.size() + 1;
    return fd;
}

std::string lotusConfigDir() {
#if LOTUS_USE_MODERN_FCITX_API
    return (fcitx::StandardPaths::global().userDirectory(fcitx::StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
    return fcitx::StandardPath::global().userDirectory(fcitx::StandardPath::Type::Config) + "/fcitx5/conf";
#endif
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isBackspace(uint32_t sym) {
    return sym == 65288 || sym == 8 || sym == FcitxKey_BackSpace;
}

uint32_t engineKeyValue(uint32_t sym) {
    const uint32_t codePoint = fcitx::Key::keySymToUnicode(static_cast<fcitx::KeySym>(sym));
    return codePoint >= 0x20U ? codePoint : sym;
}

bool isUinputMode(fcitx::LotusMode mode) {
    // Constant set, so unlike the configurable shortcuts elsewhere a static
    // table stays correct.
    static const auto modes = std::to_array<fcitx::LotusMode>({
        fcitx::LotusMode::Smooth,
        fcitx::LotusMode::SuperSmooth,
        fcitx::LotusMode::Select,
    });
    return std::ranges::find(modes, mode) != modes.end();
}

bool isChromiumProgram(std::string_view program) {
    static const auto names = std::to_array<std::string_view>({
        "chrome",
        "chromium",
        "brave",
        "edge",
        "vivaldi",
        "opera",
        "cromite",
        "helium",
        "thorium",
        "slimjet",
        "yandex",
    });
    return std::ranges::any_of(names, [program](std::string_view name) { return program.find(name) != std::string_view::npos; });
}

DiffPart compareAndSplitU32(std::u32string_view oldText, std::u32string_view newText) {
    auto [itA, itB] = std::ranges::mismatch(oldText, newText);
    size_t offset   = std::distance(oldText.begin(), itA);
    return {.deletedPart = oldText.substr(offset), .addedPart = newText.substr(offset)};
}

std::string getFrontendName(fcitx::InputContext* ic) {
    if (ic == nullptr) {
        return "unknown";
    }
    return ic->frontend();
}

std::string getEnv(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return ((value != nullptr) && ((*value) != 0)) ? value : "";
}