/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-utils.h"
#include "lotus-config.h"

#include <cstddef>
#include <fcitx-utils/utf8.h>

// Global variables
fcitx::LotusMode        realMode = fcitx::LotusMode::Smooth;
std::atomic<bool>       needEngineReset{false};
std::string             BASE_SOCKET_PATH;
std::atomic<bool>       g_mouse_clicked{false};
std::atomic<bool>       is_deleting_{false};
std::once_flag          monitor_init_flag;
std::atomic<bool>       stop_flag_monitor{false};
std::atomic<bool>       monitor_running{false};
int                     uinput_client_fd_ = -1;
unsigned int            realtextLen       = 0;
std::atomic<int>        mouse_socket_fd{-1};

std::atomic<int64_t>    replacement_start_ms_{0};
std::atomic<int>        replacement_thread_id_{0};
std::atomic<bool>       needFallbackCommit{false};

std::mutex              monitor_mutex;
std::condition_variable monitor_cv;

FCITX_DEFINE_LOG_CATEGORY(lotus, "lotus", fcitx::LogLevel::NoLog);

std::string buildSocketPath(const char* base_path_suffix) {
    const char* username_c = std::getenv("USER");
    std::string path;
    path.reserve(32);
    path += "lotussocket-";
    path += ((username_c != nullptr) ? username_c : "unknown");
    path += '-';
    path += base_path_suffix;
    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    path.resize(std::min(path.length(), max_socket_path_length));
    return path;
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isBackspace(uint32_t sym) {
    return sym == 65288 || sym == 8 || sym == FcitxKey_BackSpace;
}

int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& commonPrefix, std::string& deletedPart, std::string& addedPart) {
    size_t i = 0;
    size_t j = 0;

    while (i < A.size() && j < B.size()) {
        unsigned int lenA = fcitx_utf8_char_len(&A[i]);
        unsigned int lenB = fcitx_utf8_char_len(&B[j]);
        if (lenA == lenB && std::strncmp(&A[i], &B[j], lenA) == 0) {
            i += lenA;
            j += lenB;
        } else {
            break;
        }
    }

    commonPrefix = A.substr(0, i);
    deletedPart  = A.substr(i);
    addedPart    = B.substr(j);
    return (deletedPart.empty() && addedPart.empty()) ? 1 : 2;
}

bool isStartsWith(const std::string& str, const std::string& prefix) {
#if __cplusplus >= 202002L
    return str.starts_with(prefix);
#else
    return str.substr(0, prefix.size()) == prefix;
#endif
}