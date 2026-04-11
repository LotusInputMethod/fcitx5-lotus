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
#include <glob.h>

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

UinputDevice::~UinputDevice() {
    if (guard_.is_valid()) {
        ioctl(guard_.get(), UI_DEV_DESTROY);
    }
}

bool UinputDevice::initialize() {
    int fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0)
        return false;
    guard_.reset(fd);

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        return false;
    }
    // Enable common keyboard keys using ranges to reduce code size
    for (int k = KEY_ESC; k <= KEY_BACKSPACE; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    for (int k = KEY_TAB; k <= KEY_RIGHTBRACE; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    for (int k = KEY_ENTER; k <= KEY_APOSTROPHE; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    for (int k = KEY_GRAVE; k <= KEY_KPASTERISK; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    for (int k = KEY_LEFTALT; k <= KEY_CAPSLOCK; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    for (int k = KEY_F1; k <= KEY_F10; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    for (int k = KEY_NUMLOCK; k <= KEY_DELETE; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTMETA);

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    strncpy(usetup.name, "Lotus-Uinput-Server", UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        return false;
    }
    sleep(1);
    return true;
}

void UinputDevice::send_backspace() {
    if (!guard_.is_valid())
        return;
    struct input_event ev[4]{};
    ev[0].type  = EV_KEY;
    ev[0].code  = KEY_BACKSPACE;
    ev[0].value = 1; // Press
    ev[1].type  = EV_SYN;
    ev[1].code  = SYN_REPORT;
    ev[1].value = 0;
    ev[2].type  = EV_KEY;
    ev[2].code  = KEY_BACKSPACE;
    ev[2].value = 0; // Release
    ev[3].type  = EV_SYN;
    ev[3].code  = SYN_REPORT;
    ev[3].value = 0;
    write(guard_.get(), ev, sizeof(ev));
}

void UinputDevice::send_key(uint32_t code, uint32_t value) {
    if (!guard_.is_valid())
        return;
    if (code >= KEY_MAX)
        return;
    struct input_event ev[2]{};
    ev[0].type  = EV_KEY;
    ev[0].code  = static_cast<uint16_t>(code);
    ev[0].value = static_cast<int32_t>(value);
    ev[1].type  = EV_SYN;
    ev[1].code  = SYN_REPORT;
    ev[1].value = 0;
    write(guard_.get(), ev, sizeof(ev));
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

static bool authorize_client(int client_fd, const char* expected_exe, uid_t expected_uid) {
    struct ucred cred{};
    socklen_t    len = sizeof(struct ucred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        LotusLogger::instance().warn("Failed to get peer credentials");
        return false;
    }

    if (cred.uid != expected_uid) {
        LotusLogger::instance().warn("Unauthorized UID: " + std::to_string(cred.uid));
        return false;
    }

    char path[64], exe_path[PATH_MAX] = {0};
    snprintf(path, sizeof(path), "/proc/%d/exe", cred.pid);
    if (readlink(path, exe_path, sizeof(exe_path) - 1) == -1) {
        return false;
    }

    if (strcmp(exe_path, expected_exe) != 0) {
        LotusLogger::instance().warn("Unauthorized process: " + std::string(exe_path));
        return false;
    }

    return true;
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
    struct passwd  pw_buf{};
    struct passwd* pw = nullptr;
    char           buf[1024];
    int            res = getpwnam_r(username.c_str(), &pw_buf, buf, sizeof(buf), &pw);
    if (res == 0 && pw != nullptr) {
        return pw->pw_uid;
    }
    return (uid_t)-1;
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

static int setup_socket(const std::string& path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un addr{};
    addr.sun_family  = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], path.c_str(), path.length());
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + path.length() + 1;
    if (bind(fd, (struct sockaddr*)&addr, len) < 0 || listen(fd, 10) < 0) {
        close(fd);
        return -1;
    }
    return fd;
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

    uid_t expected_uid = get_uid_for_user(target_user);
    if (expected_uid == (uid_t)-1) {
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

    std::string osk_socket;
    osk_socket.reserve(44);
    osk_socket += "lotussocket-";
    osk_socket += target_user;
    osk_socket += "-osk_socket";

    std::string mouse_flag_socket;
    mouse_flag_socket.reserve(48);
    mouse_flag_socket += "lotussocket-";
    mouse_flag_socket += target_user;
    mouse_flag_socket += "-mouse_socket";

    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    backspace_socket.resize(std::min(backspace_socket.length(), max_socket_path_length));
    osk_socket.resize(std::min(osk_socket.length(), max_socket_path_length));
    mouse_flag_socket.resize(std::min(mouse_flag_socket.length(), max_socket_path_length));

    // Setup Uinput
    UinputDevice uinput;
    if (!uinput.initialize()) {
        LotusLogger::instance().error("Failed to initialize uinput device");
        return 1;
    }

    // Setup Sockets
    FdGuard server_fd(setup_socket(backspace_socket));
    FdGuard osk_server_fd(setup_socket(osk_socket));
    FdGuard mouse_server_fd(setup_socket(mouse_flag_socket));

    if (!server_fd.is_valid() || !osk_server_fd.is_valid() || !mouse_server_fd.is_valid()) {
        LotusLogger::instance().error("Failed to setup sockets");
        return 1;
    }

    LibinputContext li_ctx(&interface);
    if (!li_ctx.is_valid()) {
        LotusLogger::instance().error("Failed to create libinput/udev context");
        return 1;
    }

    std::vector<struct pollfd> fds;
    const int                  KB_CLIENT_INDEX  = 3;
    const int                  OSK_SERVER_INDEX = 4;
    const int                  OSK_CLIENT_INDEX = 5;
    fds.push_back({server_fd.get(), POLLIN, 0});       // [0] kb server
    fds.push_back({li_ctx.get_fd(), POLLIN, 0});       // [1] libinput
    fds.push_back({mouse_server_fd.get(), POLLIN, 0}); // [2] mouse server
    fds.push_back({-1, POLLIN, 0});                    // [3] kb client (engine)
    fds.push_back({osk_server_fd.get(), POLLIN, 0});   // [4] osk server
    fds.push_back({-1, POLLIN, 0});                    // [5] osk client

    FdGuard          addon_fd;
    FdGuard          kb_client_fd;
    FdGuard          osk_client_fd;
    int              pending_backspaces = 0;
    bool             osk_active         = false;

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
                uinput.send_backspace();
                --pending_backspaces;
            }
        }

        libinput_dispatch(li_ctx.get_li());

        // handle socket (backspace from engine)
        if ((fds[0].revents & POLLIN) != 0) {
            int client_fd = accept4(server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd >= 0) {
                if (authorize_client(client_fd, FCITX5_EXECUTABLE_PATH, expected_uid)) {
                    LotusLogger::instance().info("Fcitx5 engine connected to kb socket");
                    kb_client_fd.reset(client_fd);
                    fds[KB_CLIENT_INDEX].fd = kb_client_fd.get();
                } else {
                    close(client_fd);
                }
            }
        }

        // handle osk server (accept new OSK connection)
        if ((fds[OSK_SERVER_INDEX].revents & POLLIN) != 0) {
            int client_fd = accept4(osk_server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd >= 0) {
                if (authorize_client(client_fd, FCITX5_OSK_EXECUTABLE_PATH, expected_uid)) {
                    LotusLogger::instance().info("OSK client connected to osk socket");
                    osk_client_fd.reset(client_fd);
                    fds[OSK_CLIENT_INDEX].fd = osk_client_fd.get();
                } else {
                    close(client_fd);
                }
            }
        }

        // handle commands from engine (kb_socket: backspace, legacy int)
        if (fds[KB_CLIENT_INDEX].fd >= 0 && (fds[KB_CLIENT_INDEX].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            LotusKeyCommand cmd{};
            ssize_t         n = recv(fds[KB_CLIENT_INDEX].fd, &cmd, sizeof(cmd), 0);
            if (n <= 0) {
                LotusLogger::instance().warn("KB client disconnected or connection error");
                kb_client_fd.reset(-1);
                fds[KB_CLIENT_INDEX].fd = -1;
            } else if (static_cast<size_t>(n) == sizeof(cmd)) {
                if (cmd.type == LotusKeyCommandType::BackspaceCount) { // Backspace count
                    pending_backspaces += cmd.code - 1;
                    uinput.send_backspace();
                }
            } else if (static_cast<size_t>(n) == sizeof(int)) {
                // Legacy support for plain int (backspace count)
                int count;
                memcpy(&count, &cmd, sizeof(int));
                pending_backspaces += count - 1;
                uinput.send_backspace();
            }
        }

        // handle commands from OSK (osk_socket: key events, CapsLock query)
        if (fds[OSK_CLIENT_INDEX].fd >= 0 && (fds[OSK_CLIENT_INDEX].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            LotusKeyCommand cmd{};
            ssize_t         n = recv(fds[OSK_CLIENT_INDEX].fd, &cmd, sizeof(cmd), 0);
            if (n <= 0) {
                LotusLogger::instance().warn("OSK client disconnected or connection error");
                osk_client_fd.reset(-1);
                fds[OSK_CLIENT_INDEX].fd = -1;
                osk_active               = false; // Reset state on disconnect
            } else if (static_cast<size_t>(n) == sizeof(cmd)) {
                if (cmd.type == LotusKeyCommandType::KeyEvent) { // Universal key from OSK
                    uinput.send_key(cmd.code, cmd.value);
                } else if (cmd.type == LotusKeyCommandType::OskShow) { // OSK Show
                    osk_active = true;
                } else if (cmd.type == LotusKeyCommandType::OskHide) { // OSK Hide
                    osk_active = false;
                }
            }
        }

        // connect to mouse socket
        if ((fds[2].revents & POLLIN) != 0) {
            int new_fd = accept4(mouse_server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (new_fd >= 0) {
                LotusLogger::instance().info("New mouse flag client connected");
                addon_fd.reset(new_fd);
            }
        }

        // handle mouse (libinput)
        if ((fds[1].revents & POLLIN) != 0) {
            struct libinput_event* event = nullptr;

            while ((event = libinput_get_event(li_ctx.get_li())) != nullptr) {
                enum libinput_event_type type = libinput_event_get_type(event);

                if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
                    struct libinput_event_pointer* p = libinput_event_get_pointer_event(event);
                    if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED) {
                        if (!osk_active && addon_fd.is_valid()) {
                            if (send(addon_fd.get(), "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                                LotusLogger::instance().warn("Failed to send to mouse flag client, closing connection");
                                addon_fd.reset(-1);
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
    LotusLogger::instance().info("Terminating server...");
    return 0;
}
