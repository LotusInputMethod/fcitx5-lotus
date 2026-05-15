/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-server.h"

//#include <cstring>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

std::atomic<bool> g_running{true};

FdGuard::~FdGuard() { reset(); }

FdGuard::FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

FdGuard& FdGuard::operator=(FdGuard&& other) noexcept {
    if (this != &other) { reset(other.fd_); other.fd_ = -1; }
    return *this;
}

void FdGuard::reset(int new_fd) {
    if (fd_ >= 0) close(fd_);
    fd_ = new_fd;
}

UinputDevice::~UinputDevice() {
    if (guard_.is_valid()) ioctl(guard_.get(), UI_DEV_DESTROY);
}

bool UinputDevice::initialize() {
    int fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0) return false;
    guard_.reset(fd);

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_KEYBIT, KEY_BACKSPACE) < 0)
        return false;

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    strncpy(usetup.name, "Lotus-Uinput-Server", UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
        return false;

    sleep(1);
    return true;
}

void UinputDevice::send_backspace() {
    if (!guard_.is_valid()) return;
    static struct input_event ev[4] = {
        {.time = {}, .type = EV_KEY, .code = KEY_BACKSPACE, .value = 1},
        {.time = {}, .type = EV_SYN, .code = SYN_REPORT,   .value = 0},
        {.time = {}, .type = EV_KEY, .code = KEY_BACKSPACE, .value = 0},
        {.time = {}, .type = EV_SYN, .code = SYN_REPORT,   .value = 0},
    };
    int fd = guard_.get();
    ssize_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(1L), "D"((long)fd), "S"(ev), "d"(sizeof(ev))
        : "rcx", "r11", "memory"
    );
}

LibinputContext::LibinputContext(const struct libinput_interface* interface) : udev_(udev_new()) {
    if (udev_ != nullptr) {
        li_ = libinput_udev_create_context(interface, nullptr, udev_);
        if (li_ != nullptr) {
            if (libinput_udev_assign_seat(li_, "seat0") != 0) {
                libinput_unref(li_);
                li_ = nullptr;
            }
        }
    }
}

LibinputContext::~LibinputContext() {
    if (li_ != nullptr)   libinput_unref(li_);
    if (udev_ != nullptr) udev_unref(udev_);
}

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) g_running.store(false);
}

bool get_current_username(char* out, size_t len) {
    struct passwd pwd{};
    struct passwd* result = nullptr;
    char buf[128];
    if (getpwuid_r(getuid(), &pwd, buf, sizeof(buf), &result) == 0 && result) {
        strncpy(out, result->pw_name, len - 1);
        return true;
    }
    strncpy(out, "unknown", len - 1);
    return false;
}

uid_t get_uid_for_user(const char* username) {
    struct passwd pw_buf{};
    struct passwd* pw = nullptr;
    char buf[128];
    if (getpwnam_r(username, &pw_buf, buf, sizeof(buf), &pw) == 0 && pw)
        return pw->pw_uid;
    return (uid_t)-1;
}

void boost_process_priority() {
    setpriority(PRIO_PROCESS, 0, -10); //NOLINT
}

void pin_to_pcore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i <= 3; ++i) CPU_SET(i, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

int open_restricted(const char* path, int flags, void* /*user_data*/) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

void close_restricted(int fd, void* /*user_data*/) { close(fd); }

const struct libinput_interface interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

int main(int argc, char* argv[]) {
    char target_user[64] = {0};
    if (argc == 3 && strcmp(argv[1], "-u") == 0) { // NOLINT
        strncpy(target_user, argv[2], sizeof(target_user) - 1);
        target_user[sizeof(target_user) - 1] = '\0';
    } else {
        if (!get_current_username(target_user, sizeof(target_user))) return 1;
    }

    uid_t expected_uid = get_uid_for_user(target_user);
    if (expected_uid == (uid_t)-1) return 1;

    boost_process_priority();
    pin_to_pcore();

    char backspace_socket[128];
    char mouse_flag_socket[128];
    snprintf(backspace_socket,  sizeof(backspace_socket),  "lotussocket-%s-kb_socket",    target_user);
    snprintf(mouse_flag_socket, sizeof(mouse_flag_socket), "lotussocket-%s-mouse_socket", target_user);
    size_t kb_str_len    = strlen(backspace_socket);
    size_t mouse_str_len = strlen(mouse_flag_socket);

    UinputDevice uinput;
    if (!uinput.initialize()) return 1;

    FdGuard server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    FdGuard mouse_server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));

    struct sockaddr_un addr_kb{};
    struct sockaddr_un addr_mouse{};
    addr_kb.sun_family    = AF_UNIX;
    addr_mouse.sun_family = AF_UNIX;
    addr_kb.sun_path[0]    = '\0';
    addr_mouse.sun_path[0] = '\0';
    memcpy(&addr_kb.sun_path[1],    backspace_socket,  kb_str_len);
    memcpy(&addr_mouse.sun_path[1], mouse_flag_socket, mouse_str_len);

    socklen_t kb_len    = offsetof(struct sockaddr_un, sun_path) + kb_str_len    + 1;
    socklen_t mouse_len = offsetof(struct sockaddr_un, sun_path) + mouse_str_len + 1;

    if (bind(server_fd.get(),       (struct sockaddr*)&addr_kb,    kb_len)    != 0) return 1;
    if (bind(mouse_server_fd.get(), (struct sockaddr*)&addr_mouse, mouse_len) != 0) return 1;

    listen(server_fd.get(), 5);
    listen(mouse_server_fd.get(), 5);

    LibinputContext li_ctx(&interface);
    if (!li_ctx.is_valid()) return 1;

    constexpr int KB_CLIENT_INDEX = 3;
    struct pollfd fds[4] = {
        {server_fd.get(),       POLLIN, 0},
        {li_ctx.get_fd(),       POLLIN, 0},
        {mouse_server_fd.get(), POLLIN, 0},
        {-1,                    POLLIN, 0},
    };

    FdGuard addon_fd;
    FdGuard kb_client_fd;
    int     pending_backspaces = 0;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    int64_t last_bs_ms = 0;
    while (g_running.load(std::memory_order_acquire)) {
        int poll_timeout = (pending_backspaces > 0) ? 1 : -1;
        int ret          = poll(fds, 4, poll_timeout);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (pending_backspaces > 0) {
            struct timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now_ms = static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
            if (now_ms - last_bs_ms >= 1) {
                uinput.send_backspace();
                --pending_backspaces;
                last_bs_ms = now_ms;
                if (pending_backspaces == 0) {
                    char ack = '7';
                    send(fds[3].fd, &ack, sizeof(ack), MSG_NOSIGNAL);
                }
            }
        }

        libinput_dispatch(li_ctx.get_li());

        if ((fds[0].revents & POLLIN) != 0) {
            int client_fd = accept4(server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd >= 0) {
                struct ucred cred{};
                socklen_t    cred_len = sizeof(struct ucred);
                bool         authorized = false;

                if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0
                    && cred.uid == expected_uid)
                {
                    char path[64];
                    char exe_path[64] = {0};
                    snprintf(path, sizeof(path), "/proc/%d/exe", cred.pid);
                    ssize_t n = readlink(path, exe_path, sizeof(exe_path) - 1);
                    if (n > 0) exe_path[n] = '\0';
                    authorized = (strcmp(exe_path, "/usr/bin/fcitx5") == 0);
                }

                if (authorized) {
                    kb_client_fd.reset(client_fd);
                    fds[KB_CLIENT_INDEX].fd = kb_client_fd.get();
                } else {
                    close(client_fd);
                }
            }
        }

        if (fds[KB_CLIENT_INDEX].fd >= 0
            && (fds[KB_CLIENT_INDEX].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
        {
            int     count = 0;
            ssize_t n     = recv(fds[KB_CLIENT_INDEX].fd, &count, sizeof(count), 0);
            if (n <= 0) {
                kb_client_fd.reset(-1);
                fds[KB_CLIENT_INDEX].fd = -1;
            } else if (count > 0) {
                pending_backspaces += count - 1;
                uinput.send_backspace();
            }
        }

        if ((fds[2].revents & POLLIN) != 0) {
            int new_fd = accept4(mouse_server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (new_fd >= 0) addon_fd.reset(new_fd);
        }

        if ((fds[1].revents & POLLIN) != 0) {
            struct libinput_event* event = nullptr;
            while ((event = libinput_get_event(li_ctx.get_li())) != nullptr) {
                enum libinput_event_type type = libinput_event_get_type(event);

                if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
                    struct libinput_event_pointer* p = libinput_event_get_pointer_event(event);
                    if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED
                        && addon_fd.is_valid())
                    {
                        if (send(addon_fd.get(), "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0)
                            addon_fd.reset(-1);
                    }
                } else if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
                    struct libinput_device* dev = libinput_event_get_device(event);
                    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
                        libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
                        libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);
                    }
                }
                libinput_event_destroy(event);
            }
        }
    }
    return 0;
}
