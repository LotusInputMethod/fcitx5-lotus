/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-utils.h"
#include "lotus-fd-guard.h"
#include "lotus-config.h"

#include <cstddef>
#include <cstring>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <pwd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>

// Global variables
std::atomic<fcitx::LotusMode> realMode{fcitx::LotusMode::Smooth};
std::atomic<bool>             g_mouse_clicked{false};
std::atomic<bool>             is_deleting_{false};
std::atomic<bool>             stop_flag_monitor{false};
std::atomic<int>              uinput_client_fd_{-1};
std::atomic<unsigned int>     realtextLen{0};
std::atomic<int>              mouse_socket_fd{-1};

namespace fcitx {
    FCITX_DEFINE_LOG_CATEGORY(lotusLog, "lotus", fcitx::LogLevel::NoLog);
}

std::string buildSocketPath(std::string_view base_path_suffix) {
    struct passwd  pwd{};
    struct passwd* result   = nullptr;
    long           buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buf_size == -1) {
        buf_size = 16384;
    }
    std::vector<char> buf(buf_size);
    std::string       username;
    int               res = getpwuid_r(getuid(), &pwd, buf.data(), buf_size, &result);
    if (res == 0 && result != nullptr) {
        username = result->pw_name;
    } else {
        username = "unknown";
    }
    std::string path;
    path.reserve(32);
    path += "lotussocket-";
    path += username;
    path += '-';
    path += base_path_suffix;
    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    path.resize(std::min(path.length(), max_socket_path_length));
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

void closeUinputClient() {
    FdGuard old_fd(uinput_client_fd_.exchange(-1));
}

std::string lotusConfigDir() {
#if LOTUS_USE_MODERN_FCITX_API
    return (fcitx::StandardPaths::global().userDirectory(fcitx::StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
    return fcitx::StandardPath::global().userDirectory(fcitx::StandardPath::Type::Config) + "/fcitx5/conf";
#endif
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isBackspace(uint32_t sym) {
    return sym == 65288 || sym == 8 || sym == FcitxKey_BackSpace;
}

uint32_t engineKeyValue(uint32_t sym) {
    const uint32_t codePoint = fcitx::Key::keySymToUnicode(static_cast<fcitx::KeySym>(sym));
    return codePoint != 0U ? codePoint : sym;
}

bool isUinputMode(fcitx::LotusMode mode) {
    // Constant set, so unlike the configurable shortcuts elsewhere a static
    // table stays correct.
    static const std::array<fcitx::LotusMode, 4> modes = {
        fcitx::LotusMode::Smooth,
        fcitx::LotusMode::SuperSmooth,
        fcitx::LotusMode::Minecraft,
        fcitx::LotusMode::Select,
    };
    return std::ranges::find(modes, mode) != modes.end();
}

int compareAndSplitU32(std::u32string_view A, std::u32string_view B, std::u32string& deletedPart, std::u32string& addedPart) {
    size_t i = 0;
    size_t j = 0;
    while (i < A.size() && j < B.size() && A[i] == B[j]) {
        ++i;
        ++j;
    }

    deletedPart.assign(A, i, std::u32string_view::npos);
    addedPart.assign(B, j, std::u32string_view::npos);
    return (deletedPart.empty() && addedPart.empty()) ? 1 : 2;
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