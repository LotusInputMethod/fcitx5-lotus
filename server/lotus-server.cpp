/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-server.h"
#include "lotus-logger.h"

#include <cstring>
#include <vector>

#include <signal.h>
#include <limits.h>
#include <unistd.h>

int               uinput_fd_ = -1;
std::atomic<bool> g_running{true};

void              signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        LotusLogger::instance().info("Terminating server...");
        g_running.store(false);
    }
}

std::string get_current_username() {
    struct passwd* pw = getpwuid(getuid());
    return (pw != nullptr) ? pw->pw_name : "unknown";
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

void send_single_backspace() {
    if (uinput_fd_ < 0) {
        LotusLogger::instance().error("Uinput device not initialized");
        return;
    }
    struct input_event ev[4]{};

    // Press
    ev[0].type  = EV_KEY;
    ev[0].code  = KEY_BACKSPACE;
    ev[0].value = 1;

    // Release
    ev[2].type = EV_KEY;
    ev[2].code = KEY_BACKSPACE;
    // ev[1], ev[3] are SYN_REPORT by default

    if (write(uinput_fd_, ev, sizeof(ev)) < 0) {
        LotusLogger::instance().error("Failed to write to uinput: " + std::string(strerror(errno)));
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

int main(int argc, char* argv[]) {
    std::string target_user;
    if (argc == 3 && strcmp(argv[1], "-u") == 0) { // NOLINT
        target_user = argv[2];                     // NOLINT
    } else {
        target_user = get_current_username();
    }
    LotusLogger::instance().info("Target user: " + target_user);
    boost_process_priority();
    pin_to_pcore();

    std::string backspace_socket;
    backspace_socket.reserve(40);
    backspace_socket += "lotussocket-";
    backspace_socket += target_user;
    backspace_socket += "-kb_socket";

    std::string mouse_flag_socket;
    mouse_flag_socket.reserve(48);
    mouse_flag_socket += "lotussocket-";
    mouse_flag_socket += target_user;
    mouse_flag_socket += "-mouse_socket";

    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    backspace_socket.resize(std::min(backspace_socket.length(), max_socket_path_length));
    mouse_flag_socket.resize(std::min(mouse_flag_socket.length(), max_socket_path_length));

    // Setup Uinput
    uinput_fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd_ >= 0) {
        LotusLogger::instance().info("Uinput device initialized");
        ioctl(uinput_fd_, UI_SET_EVBIT, EV_KEY);
        ioctl(uinput_fd_, UI_SET_KEYBIT, KEY_BACKSPACE);
        struct uinput_setup usetup{};
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor  = 0x1234;
        usetup.id.product = 0x5678;
        strncpy(usetup.name, "Lotus-Uinput-Server", UINPUT_MAX_NAME_SIZE - 1);
        ioctl(uinput_fd_, UI_DEV_SETUP, &usetup);
        ioctl(uinput_fd_, UI_DEV_CREATE);
        sleep(1);
    } else {
        LotusLogger::instance().error("Failed to initialize uinput device");
        return 1;
    }

    int                server_fd       = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int                mouse_server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

    struct sockaddr_un addr_kb{};
    struct sockaddr_un addr_mouse{};

    addr_kb.sun_family    = AF_UNIX;
    addr_mouse.sun_family = AF_UNIX;

    addr_kb.sun_path[0]    = '\0';
    addr_mouse.sun_path[0] = '\0';

    memcpy(&addr_kb.sun_path[1], backspace_socket.c_str(), backspace_socket.length());
    memcpy(&addr_mouse.sun_path[1], mouse_flag_socket.c_str(), mouse_flag_socket.length());

    socklen_t kb_len    = offsetof(struct sockaddr_un, sun_path) + backspace_socket.length() + 1;
    socklen_t mouse_len = offsetof(struct sockaddr_un, sun_path) + mouse_flag_socket.length() + 1;

    if (bind(server_fd, (struct sockaddr*)&addr_kb, kb_len) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    if (bind(mouse_server_fd, (struct sockaddr*)&addr_mouse, mouse_len) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    listen(server_fd, 5);
    listen(mouse_server_fd, 5);

    struct udev*     udev = udev_new();
    struct libinput* li   = libinput_udev_create_context(&interface, nullptr, udev);
    if (udev == nullptr) {
        LotusLogger::instance().error("Failed to create udev context");
        return 1;
    }
    if (li == nullptr) {
        LotusLogger::instance().error("Failed to create libinput context");
        udev_unref(udev);
        return 1;
    }
    libinput_udev_assign_seat(li, "seat0");
    int                        li_fd = libinput_get_fd(li);

    std::vector<struct pollfd> fds;
    const int                  KB_CLIENT_INDEX = 3;
    fds.push_back({server_fd, POLLIN, 0});
    fds.push_back({li_fd, POLLIN, 0});
    fds.push_back({mouse_server_fd, POLLIN, 0});
    fds.push_back({-1, POLLIN, 0}); // Keyboard socket client

    int              addon_fd           = -1;
    int              pending_backspaces = 0;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    while (g_running.load(std::memory_order_acquire)) {
        int poll_timeout = (pending_backspaces > 0) ? 1 : -1;
        int ret          = poll(fds.data(), fds.size(), poll_timeout);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ret == 0) {
            if (pending_backspaces > 0) {
                send_single_backspace();
                --pending_backspaces;
            }
        }

        libinput_dispatch(li);

        // handle socket (backspace)
        if ((fds[0].revents & POLLIN) != 0) {
            int client_fd = accept4(server_fd, nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd >= 0) {
                struct ucred cred{};
                socklen_t    len                = sizeof(struct ucred);
                char         exe_path[PATH_MAX] = {0};

                if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
                    char path[64];
                    snprintf(path, sizeof(path), "/proc/%d/exe", cred.pid);

                    ssize_t ret = readlink(path, exe_path, sizeof(exe_path) - 1);
                    if (ret != -1) {
                        exe_path[ret] = '\0'; // NOLINT
                    }
                }

                if (strcmp(exe_path, "/usr/bin/fcitx5") == 0) {
                    LotusLogger::instance().info("Fcitx5 connected to keyboard socket (PID: " + std::to_string(cred.pid) + ")");
                    if (fds[KB_CLIENT_INDEX].fd >= 0) {
                        close(fds[KB_CLIENT_INDEX].fd);
                    }
                    fds[KB_CLIENT_INDEX].fd = client_fd;
                } else {
                    LotusLogger::instance().warn("Unauthorized connection attempt from: " + std::string(exe_path));
                    close(client_fd);
                }
            }
        }

        // handle connect from addon
        if (fds[KB_CLIENT_INDEX].fd >= 0 && (fds[KB_CLIENT_INDEX].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            int     count = 0;
            ssize_t n     = recv(fds[KB_CLIENT_INDEX].fd, &count, sizeof(count), 0);
            if (n <= 0) {
                LotusLogger::instance().warn("Keyboard client disconnected or connection error");
                close(fds[KB_CLIENT_INDEX].fd);
                fds[KB_CLIENT_INDEX].fd = -1;
            } else {
                pending_backspaces += count - 1;
                send_single_backspace();
            }
        }

        // connect to mouse socket
        if ((fds[2].revents & POLLIN) != 0) {
            int new_fd = accept4(mouse_server_fd, nullptr, nullptr, SOCK_NONBLOCK);
            if (new_fd >= 0) {
                LotusLogger::instance().info("New mouse flag client connected");
                if (addon_fd >= 0)
                    close(addon_fd);
                addon_fd = new_fd;
            }
        }

        // handle mouse (libinput)
        if ((fds[1].revents & POLLIN) != 0) {
            struct libinput_event* event = nullptr;

            while ((event = libinput_get_event(li)) != nullptr) {
                enum libinput_event_type type = libinput_event_get_type(event);

                if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
                    struct libinput_event_pointer* p = libinput_event_get_pointer_event(event);
                    if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED) {
                        if (addon_fd >= 0) {
                            if (send(addon_fd, "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                                LotusLogger::instance().warn("Failed to send to mouse flag client, closing connection");
                                close(addon_fd);
                                addon_fd = -1;
                            }
                        }
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
    }

    // Cleanup
    libinput_unref(li);
    udev_unref(udev);
    if (uinput_fd_ >= 0) {
        ioctl(uinput_fd_, UI_DEV_DESTROY);
        close(uinput_fd_);
    }
    if (addon_fd >= 0) {
        close(addon_fd);
    }
    if (fds[KB_CLIENT_INDEX].fd >= 0) {
        close(fds[KB_CLIENT_INDEX].fd);
    }
    close(server_fd);
    close(mouse_server_fd);
    return 0;
}
