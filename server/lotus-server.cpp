/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-abstract-socket.h"
#include "lotus-server.h"
#include "lotus-server-socket.h"
#include "lotus-logger.h"
#include "common-executable-path.h"
#include "common-fd-guard.h"
#include "common-protocol.h"
#include "common-socket-name.h"
#include "lotus-physical-shift.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <utility>
#include <csignal>
#include <cstddef>

#include <climits> // IWYU pragma: keep
#include <sched.h>
#include <unistd.h>

std::atomic<bool> g_running{true};

bool              UinputDevice::initialize() {
    dev_ = buildLotusUinputDevice();
    if (dev_ == nullptr) {
        LotusLogger::instance().error("Failed to build the uinput device description");
        return false;
    }

    libevdev_uinput* raw = nullptr;
    const int        err = libevdev_uinput_create_from_device(dev_.get(), LIBEVDEV_UINPUT_OPEN_MANAGED, &raw);
    if (err != 0) {
        LotusLogger::instance().error("Failed to create the uinput device: " + std::string(strerror(-err)));
        return false;
    }
    uidev_.reset(raw);
    if (const char* node = libevdev_uinput_get_devnode(uidev_.get()); node != nullptr) {
        for (int waited = 0; waited < 1000 && access(node, R_OK) != 0; waited += 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        LotusLogger::instance().info("Uinput device ready: " + std::string(node));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LotusLogger::instance().info("Uinput device ready (device node unknown)");
    }
    return true;
}

void UinputDevice::reportWriteFailure(int error) {
    static bool reported = false;
    if (!reported) {
        reported = true;
        LotusLogger::instance().error("Failed to write an uinput event: " + std::string(strerror(-error)));
    }
}

void UinputDevice::sendTap(uint16_t code) {
    if (uidev_ == nullptr) {
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_KEY, code, 1); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_SYN, SYN_REPORT, 0); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_KEY, code, 0); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_SYN, SYN_REPORT, 0); err != 0) {
        reportWriteFailure(err);
    }
}

void UinputDevice::sendMod(uint16_t code, int value) {
    if (uidev_ == nullptr) {
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_KEY, code, value); err != 0) {
        reportWriteFailure(err);
        return;
    }
    if (const int err = libevdev_uinput_write_event(uidev_.get(), EV_SYN, SYN_REPORT, 0); err != 0) {
        reportWriteFailure(err);
    }
}

void UinputDevice::sendShiftDown(uint16_t code) {
    sendMod(code, 1);
}

void UinputDevice::sendShiftUp(uint16_t code) {
    sendMod(code, 0);
}

LibinputContext::LibinputContext(const struct libinput_interface* interface) : udev_(udev_new()) {
    if (udev_ == nullptr) {
        return;
    }
    li_.reset(libinput_udev_create_context(interface, nullptr, udev_.get()));
    if (li_ != nullptr && libinput_udev_assign_seat(li_.get(), "seat0") != 0) {
        li_.reset();
    }
}

void signalHandler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running.store(false, std::memory_order_release);
    }
}

void boostProcessPriority() {
    if (setpriority(PRIO_PROCESS, 0, -10) != 0) { //NOLINT
        LotusLogger::instance().error("Failed to boost process priority");
    }
}

void pinToPcore() {
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

std::optional<pid_t> authorizePeer(int fd, std::string_view socketLabel) {
    struct ucred cred{};
    socklen_t    len = sizeof(struct ucred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        LotusLogger::instance().warn("Failed to get peer credentials for " + std::string(socketLabel));
        return std::nullopt;
    }

    const std::string          exeLink = "/proc/" + std::to_string(cred.pid) + "/exe";
    std::array<char, PATH_MAX> exePath{};
    const ssize_t              length = ::readlink(exeLink.c_str(), exePath.data(), exePath.size() - 1);
    if (length <= 0) {
        LotusLogger::instance().warn("Failed to resolve the peer executable for " + std::string(socketLabel));
        return std::nullopt;
    }

    const std::string_view exe(exePath.data(), static_cast<std::size_t>(length));
    if (!isAllowedExecutablePath(exe, LOTUS_FCITX5_EXECUTABLE, LOTUS_ALT_EXECUTABLE_PREFIX)) {
        LotusLogger::instance().warn("Unauthorized executable connection attempt to " + std::string(socketLabel) + " from: " + std::string(exe));
        return std::nullopt;
    }
    return cred.pid;
}

int main() {
    boostProcessPriority();
    pinToPcore();

    const AbstractSocket kbSocket    = makeAbstractSocket(lotusSocketName("kbSocket"));
    const AbstractSocket mouseSocket = makeAbstractSocket(lotusSocketName("mouseSocket"));

    UinputDevice         uinput;
    PhysicalShiftState   physical_shift;
    if (!uinput.initialize()) {
        LotusLogger::instance().error("Failed to initialize uinput device");
        return 1;
    }

    FdGuard serverFd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));
    FdGuard mouseServerFd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));

    if (!bindServerSocket(serverFd.get(), kbSocket, "keyboard socket")) {
        return 1;
    }

    if (!bindServerSocket(mouseServerFd.get(), mouseSocket, "mouse socket")) {
        return 1;
    }

    if (!listenServerSocket(serverFd.get(), "keyboard socket")) {
        return 1;
    }
    if (!listenServerSocket(mouseServerFd.get(), "mouse socket")) {
        return 1;
    }

    LibinputContext li_ctx(&interface);
    if (!li_ctx.isValid()) {
        LotusLogger::instance().error("Failed to create libinput/udev context");
        return 1;
    }

    constexpr std::size_t kKeyboardServerIndex = 0U; ///< Slot of the keyboard socket server fd
    constexpr std::size_t kMouseServerIndex    = 2U; ///< Slot of the mouse socket server fd
    constexpr std::size_t kKbClientIndex       = 3U; ///< Slot of the connected keyboard socket client

    auto                  fds = std::to_array<struct pollfd>({
        {.fd = serverFd.get(), .events = POLLIN, .revents = 0},
        {.fd = li_ctx.getFd(), .events = POLLIN, .revents = 0},
        {.fd = mouseServerFd.get(), .events = POLLIN, .revents = 0},
        {.fd = -1, .events = POLLIN, .revents = 0},
    });

    FdGuard               addonFd;
    FdGuard               kbClientFd;
    size_t                pendingBackspaces   = 0;
    size_t                pendingSelects      = 0;     ///< remaining Shift+Left events to pace out
    bool                  pendingAnchor       = false; ///< anchor key still to be injected after the backspace burst
    bool                  pendingSelectAnchor = false; ///< Shift release + Delete + anchor still to run after the left burst
    bool                  shiftHeld           = false; ///< the server pressed KEY_LEFTSHIFT for an in-flight selection

    uint32_t              currentPostDelay = 0; ///< stores postDelay from KbMsg
    uint32_t              currentInterval  = 5; ///< ms between injected keys within one request

    struct sigaction      sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    int exitCode = 0;

    while (g_running.load(std::memory_order_acquire)) {

        int pollTimeout = -1;
        if (pendingBackspaces > 0 || pendingSelects > 0 || pendingAnchor || pendingSelectAnchor) {
            pollTimeout = static_cast<int>(currentInterval);
        }

        int ret = poll(fds.data(), fds.size(), pollTimeout);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ret == 0) {
            if (pendingBackspaces > 0) {
                uinput.sendTap(KEY_BACKSPACE);
                --pendingBackspaces;
            } else if (pendingAnchor) {
                if (currentPostDelay > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(currentPostDelay));
                }
                uinput.sendTap(KEY_PAUSE);
                pendingAnchor = false;
            } else if (pendingSelects > 0) {
                uinput.sendTap(KEY_LEFT);
                --pendingSelects;
            } else if (pendingSelectAnchor) {
                if (shiftHeld) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    uinput.sendShiftUp(KEY_LEFTSHIFT);
                    shiftHeld = false;
                } else {
                    uinput.sendTap(KEY_DELETE);
                    if (currentPostDelay > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(currentPostDelay));
                    }
                    uinput.sendTap(KEY_PAUSE);
                    pendingSelectAnchor = false;
                }
            }
        }

        const int dispatchRc = libinput_dispatch(li_ctx.getLi());
        if (dispatchRc != 0) {
            LotusLogger::instance().error("Failed to dispatch libinput events: " + std::string(strerror(-dispatchRc)));
            exitCode = 1;
            break;
        }

        if ((static_cast<unsigned>(fds[kKeyboardServerIndex].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            FdGuard candidate(accept4(serverFd.get(), nullptr, nullptr, SOCK_NONBLOCK));
            if (candidate.isValid()) {
                const std::optional<pid_t> peer = authorizePeer(candidate.get(), "keyboard socket");

                if (peer.has_value()) {
                    LotusLogger::instance().info("Fcitx5 connected to keyboard socket (PID: " + std::to_string(*peer) + ")");
                    kbClientFd             = std::move(candidate);
                    fds[kKbClientIndex].fd = kbClientFd.get();
                }
            }
        }

        // handle connect from addon
        if (fds[kKbClientIndex].fd >= 0 &&
            (static_cast<unsigned>(fds[kKbClientIndex].revents) & (static_cast<unsigned>(POLLIN) | static_cast<unsigned>(POLLHUP) | static_cast<unsigned>(POLLERR))) != 0) {
            KbMsg   msg{};
            ssize_t n = recv(fds[kKbClientIndex].fd, &msg, sizeof(msg), 0);
            if (n <= 0) {
                LotusLogger::instance().warn("Keyboard client disconnected or connection error");
                if (shiftHeld) {
                    uinput.sendShiftUp(KEY_LEFTSHIFT); // never leave Shift stuck down
                    shiftHeld = false;
                }
                pendingSelects      = 0;
                pendingSelectAnchor = false;
                kbClientFd.reset(-1);
                fds[kKbClientIndex].fd = -1;
            } else if (std::cmp_not_equal(n, sizeof(KbMsg)) && std::cmp_not_equal(n, sizeof(int32_t))) {
                LotusLogger::instance().warn("Malformed keyboard message (" + std::to_string(n) + " bytes)");
            } else {
                currentInterval             = msg.interval > 0 ? std::min<uint32_t>(msg.interval, 1000) : 5;
                const ShiftState shiftState = physical_shift.shiftHeld();
                const KbOp       op         = resolveKeyboardOp(msg.op, shiftState);
                if (op != msg.op) {
                    if (shiftState == ShiftState::Held) {
                        LotusLogger::instance().info("Shift is held: replacing with Backspace instead of Shift+Left");
                    } else {
                        LotusLogger::instance().info("Shift state unknown, using safe Backspace fallback instead of Shift+Left");
                    }
                }
                if (op == KbOp::Cancel) {
                    // Finish the burst instead of leaving a partial selection behind: the
                    // Select handler taps its first Shift+Left before any later message
                    // (same socket, FIFO order), so a selection already exists here.
                    if (pendingSelectAnchor) {
                        if (pendingSelects > 0) {
                            if (!shiftHeld) {
                                uinput.sendShiftDown(KEY_LEFTSHIFT);
                                shiftHeld = true;
                            }
                            while (pendingSelects > 0) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(currentInterval));
                                uinput.sendTap(KEY_LEFT);
                                --pendingSelects;
                            }
                        }
                        if (shiftHeld) {
                            uinput.sendShiftUp(KEY_LEFTSHIFT);
                            shiftHeld = false;
                        }
                        uinput.sendTap(KEY_DELETE);
                    } else if (shiftHeld) {
                        uinput.sendShiftUp(KEY_LEFTSHIFT);
                        shiftHeld = false;
                    }
                    pendingBackspaces   = 0;
                    pendingSelects      = 0;
                    pendingAnchor       = false;
                    pendingSelectAnchor = false;
                } else if (msg.count > 0 && op == KbOp::Select) {
                    if (msg.preDelay > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(msg.preDelay));
                    }
                    currentPostDelay = msg.postDelay;
                    if (!shiftHeld) {
                        uinput.sendShiftDown(KEY_LEFTSHIFT);
                        shiftHeld = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(currentInterval));
                    uinput.sendTap(KEY_LEFT);
                    // A previous burst may still be draining.
                    pendingSelects += msg.count - 1;
                    pendingSelectAnchor = true;
                    pendingAnchor       = false;

                } else if (msg.count > 0 && op == KbOp::Backspace) {
                    if (msg.preDelay > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(msg.preDelay));
                    }
                    currentPostDelay = msg.postDelay;
                    if (shiftHeld) {
                        uinput.sendShiftUp(KEY_LEFTSHIFT);
                        shiftHeld           = false;
                        pendingSelects      = 0;
                        pendingSelectAnchor = false;
                    }
                    pendingAnchor = true;
                    pendingBackspaces += msg.count - 1;
                    uinput.sendTap(KEY_BACKSPACE);
                } else if (msg.count > 0) {
                    LotusLogger::instance().warn("Unknown keyboard message op: " + std::to_string(static_cast<int>(msg.op)));
                }
            }
        }

        if ((static_cast<unsigned>(fds[kMouseServerIndex].revents) & static_cast<unsigned>(POLLIN)) != 0) {
            FdGuard candidate(accept4(mouseServerFd.get(), nullptr, nullptr, SOCK_NONBLOCK));
            if (candidate.isValid()) {
                const std::optional<pid_t> peer = authorizePeer(candidate.get(), "mouse socket");

                if (peer.has_value()) {
                    LotusLogger::instance().info("New mouse flag client connected");
                    addonFd = std::move(candidate);
                }
            }
        }

        // libinput_dispatch() may have already consumed the fd in an iteration where
        // the libinput revents were 0 (poll timeout, or another fd woke us up).  Always
        // drain the queue; libinput_get_event() returns nullptr when it is empty.
        while (LibinputEvent event{libinput_get_event(li_ctx.getLi())}) {
            switch (libinput_event_get_type(event.get())) {
                case LIBINPUT_EVENT_POINTER_BUTTON: {
                    struct libinput_event_pointer* pointer = libinput_event_get_pointer_event(event.get());
                    if (libinput_event_pointer_get_button_state(pointer) == LIBINPUT_BUTTON_STATE_PRESSED && addonFd.isValid() &&
                        send(addonFd.get(), "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                        LotusLogger::instance().warn("Failed to send to mouse flag client, closing connection");
                        addonFd.reset(-1);
                    }
                    break;
                }
                case LIBINPUT_EVENT_DEVICE_ADDED: {
                    struct libinput_device* device  = libinput_event_get_device(event.get());
                    const char* const       rawName = libinput_device_get_name(device);
                    const std::string_view  name    = rawName != nullptr ? std::string_view(rawName) : std::string_view{};
                    LotusLogger::instance().info("Device added: " + std::string(name));
                    physical_shift.trackDevice(device);
                    if (libinput_device_config_tap_get_finger_count(device) > 0) {
                        libinput_device_config_tap_set_enabled(device, LIBINPUT_CONFIG_TAP_ENABLED);
                        libinput_device_config_tap_set_button_map(device, LIBINPUT_CONFIG_TAP_MAP_LRM);
                    }
                    break;
                }
                case LIBINPUT_EVENT_DEVICE_REMOVED: {
                    physical_shift.forgetDevice(libinput_event_get_device(event.get()));
                    break;
                }
                default: break;
            }
        }
    }
    if (shiftHeld) {
        uinput.sendShiftUp(KEY_LEFTSHIFT);
    }
    LotusLogger::instance().info("Terminating server...");
    return exitCode;
}
