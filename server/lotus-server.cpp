/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-server.h"
#include "lotus-eis-server.h"
#include "lotus-logger.h"
#include "../src/lotus-executable-path.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <csignal>
#include <cstddef>

#include <sys/socket.h>

#include <climits> // IWYU pragma: keep
#include <sched.h>
#include <sys/un.h>
#include <unistd.h>

std::atomic<bool> g_running{true};

FdGuard::~FdGuard() {
    reset();
}

FdGuard::FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

FdGuard& FdGuard::operator=(FdGuard&& other) noexcept {
    if (this != &other) {
        reset(other.fd_);
        other.fd_ = -1;
    }
    return *this;
}

void FdGuard::reset(int new_fd) {
    if (fd_ >= 0)
        close(fd_);
    fd_ = new_fd;
}

LibinputContext::LibinputContext(const struct libinput_interface* interface) : udev_(udev_new()) {
    if (udev_ != nullptr) {
        li_ = libinput_udev_create_context(interface, nullptr, udev_);
        if (li_ != nullptr && libinput_udev_assign_seat(li_, "seat0") != 0) {
            libinput_unref(li_);
            li_ = nullptr;
        }
    }
}

LibinputContext::~LibinputContext() {
    if (li_ != nullptr)
        libinput_unref(li_);
    if (udev_ != nullptr)
        udev_unref(udev_);
}

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running.store(false);
    }
}

std::string get_current_username() {
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
    return username;
}

uid_t get_uid_for_user(const std::string& username) {
    struct passwd          pw_buf{};
    struct passwd*         pw = nullptr;
    std::array<char, 1024> buf{};
    int                    res = getpwnam_r(username.c_str(), &pw_buf, buf.data(), buf.size(), &pw);
    if (res == 0 && pw != nullptr) {
        return pw->pw_uid;
    }
    return static_cast<uid_t>(-1);
}

void boost_process_priority() {
    if (setpriority(PRIO_PROCESS, 0, -10) != 0) { //NOLINT
        LotusLogger::instance().error("Failed to boost process priority");
    }
}

void pin_to_pcore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i <= 3; ++i)
        CPU_SET(i, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        LotusLogger::instance().error("Failed to pin process to core");
    }
}

int open_restricted(const char* path, int flags, void* /*user_data*/) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

void close_restricted(int fd, void* /*user_data*/) {
    close(fd);
}

const struct libinput_interface interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

namespace {

// Passes an EIS client fd over the authed socket (SCM_RIGHTS).
bool sendFd(int sock, int fd) {
    char         dummy = 0;
    struct iovec iov{&dummy, 1};
    char         cmsgbuf[CMSG_SPACE(sizeof(int))]{};
    struct msghdr msg{};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    struct cmsghdr* c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level     = SOL_SOCKET;
    c->cmsg_type      = SCM_RIGHTS;
    c->cmsg_len       = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(fd));
    return sendmsg(sock, &msg, 0) > 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string target_user;
    if (argc == 3 && strcmp(argv[1], "-u") == 0) { // NOLINT
        target_user = argv[2];                     // NOLINT
    } else {
        target_user = get_current_username();
    }
    LotusLogger::instance().info("Target user: " + target_user);

    uid_t expected_uid = get_uid_for_user(target_user);
    if (expected_uid == static_cast<uid_t>(-1)) {
        LotusLogger::instance().error("Failed to find UID for target user: " + target_user);
        return 1;
    }

    boost_process_priority();
    pin_to_pcore();

    std::string backspace_socket;
    backspace_socket.reserve(40);
    backspace_socket += "lotussocket-";
    backspace_socket += target_user;
    backspace_socket += "-kb_socket";
    // Auth gate for the EIS handshake: $LOTUS_EIS_SOCKET overrides the default.
    if (const char* env = getenv("LOTUS_EIS_SOCKET"); env != nullptr && *env != '\0')
        backspace_socket = env;

    std::string mouse_flag_socket;
    mouse_flag_socket.reserve(48);
    mouse_flag_socket += "lotussocket-";
    mouse_flag_socket += target_user;
    mouse_flag_socket += "-mouse_socket";

    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    backspace_socket.resize(std::min(backspace_socket.length(), max_socket_path_length));
    mouse_flag_socket.resize(std::min(mouse_flag_socket.length(), max_socket_path_length));

    // uinput nodes are created per EIS seat bind (see handleEisEvents).

    FdGuard            server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    FdGuard            mouse_server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));

    struct sockaddr_un addr_kb{};
    struct sockaddr_un addr_mouse{};

    addr_kb.sun_family    = AF_UNIX;
    addr_mouse.sun_family = AF_UNIX;

    addr_kb.sun_path[0]    = '\0';
    addr_mouse.sun_path[0] = '\0';

    memcpy(&addr_kb.sun_path[1], backspace_socket.c_str(), backspace_socket.length());
    memcpy(&addr_mouse.sun_path[1], mouse_flag_socket.c_str(), mouse_flag_socket.length());

    socklen_t kb_len    = offsetof(sockaddr_un, sun_path) + backspace_socket.length() + 1;
    socklen_t mouse_len = offsetof(sockaddr_un, sun_path) + mouse_flag_socket.length() + 1;

    if (bind(server_fd.get(), reinterpret_cast<struct sockaddr*>(&addr_kb), kb_len) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    if (bind(mouse_server_fd.get(), reinterpret_cast<struct sockaddr*>(&addr_mouse), mouse_len) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    listen(server_fd.get(), 5);
    listen(mouse_server_fd.get(), 5);

    LibinputContext li_ctx(&interface);
    if (!li_ctx.is_valid()) {
        LotusLogger::instance().error("Failed to create libinput/udev context");
        return 1;
    }

    EisKeyboardServer eisServer;
    if (!eisServer.initialize()) {
        LotusLogger::instance().error("Failed to initialize EIS backend");
        return 1;
    }

    std::vector<struct pollfd> fds;
    const int                  EIS_INDEX = 3;
    fds.push_back({server_fd.get(), POLLIN, 0});
    fds.push_back({li_ctx.get_fd(), POLLIN, 0});
    fds.push_back({mouse_server_fd.get(), POLLIN, 0});
    fds.push_back({eisServer.eventFd(), POLLIN, 0}); // EIS events

    FdGuard addon_fd;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // Dumb pipe: every EIS key event is emitted immediately. No pacing, no
    // sleeps — timing lives in the client's KeyPacer. poll() blocks indefinitely.
    while (g_running.load(std::memory_order_acquire)) {
        int ret = poll(fds.data(), fds.size(), -1);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        libinput_dispatch(li_ctx.get_li());

        // handle socket (backspace)
        if ((static_cast<unsigned>(fds[0].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            int client_fd = accept4(server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd >= 0) {
                struct ucred               cred{};
                socklen_t                  len = sizeof(struct ucred);
                std::array<char, PATH_MAX> exe_path{};

                bool                       authorized = false;
                if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
                    if (cred.uid == expected_uid) {
                        std::array<char, 64> path{};
                        snprintf(path.data(), path.size(), "/proc/%d/exe", cred.pid);

                        ssize_t ret = readlink(path.data(), exe_path.data(), exe_path.size() - 1);
                        if (ret != -1) {
                            exe_path[static_cast<size_t>(ret)] = '\0'; // NOLINT
                        }

                        if (isAllowedExecutablePath(exe_path.data(), LOTUS_FCITX5_EXECUTABLE, LOTUS_ALT_EXECUTABLE_PREFIX)) {
                            authorized = true;
                        } else {
                            LotusLogger::instance().warn("Unauthorized executable connection attempt to keyboard socket from: " + std::string(exe_path.data()));
                        }
                    } else {
                        LotusLogger::instance().warn("Unauthorized UID connection attempt to keyboard socket from UID: " + std::to_string(cred.uid));
                    }
                } else {
                    LotusLogger::instance().warn("Failed to get peer credentials for keyboard socket");
                }

                if (authorized) {
                    LotusLogger::instance().info("Fcitx5 connected to keyboard socket (PID: " + std::to_string(cred.pid) + "), passing EIS fd");
                    const int eisFd = eisServer.addClient();
                    if (eisFd < 0 || !sendFd(client_fd, eisFd)) {
                        LotusLogger::instance().warn("Failed to hand off EIS fd to keyboard client");
                    }
                    if (eisFd >= 0)
                        close(eisFd);
                    close(client_fd);
                } else {
                    close(client_fd);
                }
            }
        }

        // handle EIS keyboard events from addons (dumb emit, no pacing)
        if ((static_cast<unsigned>(fds[EIS_INDEX].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            eisServer.dispatch();
        }

        // connect to mouse socket
        if ((static_cast<unsigned>(fds[2].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            int new_fd = accept4(mouse_server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (new_fd >= 0) {
                struct ucred               cred{};
                socklen_t                  len = sizeof(struct ucred);
                std::array<char, PATH_MAX> exe_path{};

                bool                       authorized = false;
                if (getsockopt(new_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
                    if (cred.uid == expected_uid) {
                        std::array<char, 64> path{};
                        snprintf(path.data(), path.size(), "/proc/%d/exe", cred.pid);

                        ssize_t ret = readlink(path.data(), exe_path.data(), exe_path.size() - 1);
                        if (ret != -1) {
                            exe_path[static_cast<size_t>(ret)] = '\0'; // NOLINT
                        }

                        if (isAllowedExecutablePath(exe_path.data(), LOTUS_FCITX5_EXECUTABLE, LOTUS_ALT_EXECUTABLE_PREFIX)) {
                            authorized = true;
                        } else {
                            LotusLogger::instance().warn("Unauthorized executable connection attempt to mouse socket from: " + std::string(exe_path.data()));
                        }
                    } else {
                        LotusLogger::instance().warn("Unauthorized UID connection attempt to mouse socket from UID: " + std::to_string(cred.uid));
                    }
                } else {
                    LotusLogger::instance().warn("Failed to get peer credentials for mouse socket");
                }

                if (authorized) {
                    LotusLogger::instance().info("New mouse flag client connected");
                    addon_fd.reset(new_fd);
                } else {
                    close(new_fd);
                }
            }
        }

        // handle mouse (libinput)
        // libinput_dispatch() may have already consumed the fd in an iteration where
        // fds[1].revents was 0 (poll timeout, or another fd woke us up).  Always drain
        // the queue; libinput_get_event() returns nullptr when it is empty.
        struct libinput_event* event = nullptr;

        while ((event = libinput_get_event(li_ctx.get_li())) != nullptr) {
            enum libinput_event_type type = libinput_event_get_type(event);

            if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
                struct libinput_event_pointer* p = libinput_event_get_pointer_event(event);
                if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED && addon_fd.is_valid() &&
                    send(addon_fd.get(), "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                    LotusLogger::instance().warn("Failed to send to mouse flag client, closing connection");
                    addon_fd.reset(-1);
                }
            } else if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
                struct libinput_device* dev  = libinput_event_get_device(event);
                const char*             name = libinput_device_get_name(dev);
                LotusLogger::instance().info("Device added: " + std::string(name));
                if (libinput_device_config_tap_get_finger_count(dev) > 0) {
                    libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
                    libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);
                }
            }
            libinput_event_destroy(event);
        }
    }
    LotusLogger::instance().info("Terminating server...");
    return 0;
}
