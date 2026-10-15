/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-server.h
 * @brief Uinput server for handling backspace and mouse events.
 *
 * This server runs as a separate process with elevated privileges to:
 * - Send backspace key events via uinput device
 * - Monitor mouse button presses via libinput
 * - Communicate with fcitx5 addon via Unix sockets
 */

#ifndef _LOTUS_SERVER_H_
#define _LOTUS_SERVER_H_

#include "lotus-uinput-device.h"

#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <libinput.h>
#include <memory>
#include <optional>
#include <poll.h>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>

/**
 * @brief RAII Wrapper for the virtual keyboard injected through libevdev-uinput.
 */
class UinputDevice {
  public:
    UinputDevice() = default;

    bool initialize();
    void send_backspace();
    void send_delete();
    void send_shift_down();
    void send_shift_up();
    void send_left();
    /**
     * @brief Emits one key event (press or release) with its SYN, for the
     * EIS server's dumb per-event path.
     * @param code Linux KEY_* code.
     * @param value 1 = press, 0 = release.
     */
    void send_key(uint16_t code, int value);

  private:
    /**
     * @brief Emits a press+release batch for one key (press, SYN, release, SYN),
     *        same style as the original backspace write.
     * @param code Linux KEY_* code.
     */
    void send_tap(uint16_t code);
    /**
     * @brief Emits a single modifier-style event with its SYN.
     * @param code Linux KEY_* code.
     * @param value 1 = press, 0 = release.
     */
    void send_mod(uint16_t code, int value);
    /**
     * @brief Logs the first write failure only, so a broken device cannot flood the log.
     * @param error Negative errno returned by libevdev.
     */
    static void    reportWriteFailure(int error);
    LibevdevDevice dev_;   ///< In-memory description of the virtual keyboard
    LibevdevUinput uidev_; ///< Device created from that description
};

/**
 * @brief Unrefs a udev context.
 */
struct UdevDeleter {
    /**
     * @brief Releases the udev context.
     * @param context Context to unref, may be null.
     */
    void operator()(struct udev* context) const noexcept {
        udev_unref(context);
    }
};

/**
 * @brief Unrefs a libinput context.
 */
struct LibinputDeleter {
    /**
     * @brief Releases the libinput context.
     * @param context Context to unref, may be null.
     */
    void operator()(struct libinput* context) const noexcept {
        libinput_unref(context);
    }
};

/**
 * @brief Destroys a libinput event.
 */
struct LibinputEventDeleter {
    /**
     * @brief Releases the event handle.
     * @param event Event to destroy, may be null.
     */
    void operator()(struct libinput_event* event) const noexcept {
        libinput_event_destroy(event);
    }
};

/**
 * @brief Owning handle of one dispatched libinput event.
 */
using LibinputEvent = std::unique_ptr<struct libinput_event, LibinputEventDeleter>;

/**
 * @brief RAII Wrapper for Libinput and Udev contexts.
 */
class LibinputContext {
  public:
    explicit LibinputContext(const struct libinput_interface* interface);

    [[nodiscard]] bool is_valid() const {
        return li_ != nullptr;
    }
    [[nodiscard]] struct libinput* get_li() const {
        return li_.get();
    }
    [[nodiscard]] int get_fd() const {
        return li_ == nullptr ? -1 : libinput_get_fd(li_.get());
    }

  private:
    std::unique_ptr<struct udev, UdevDeleter>         udev_; ///< Udev context backing the libinput context
    std::unique_ptr<struct libinput, LibinputDeleter> li_;   ///< Libinput context, null when creation failed
};

/**
 * @brief Global flag to control server running state.
 */
extern std::atomic<bool> g_running; //NOLINT

/**
 * @brief Signal handler for graceful shutdown.
 * @param sig Signal number (SIGTERM or SIGINT).
 */
void signal_handler(int sig);

/**
 * @brief Gets the current username.
 * @return Username string or "unknown" if cannot determine.
 */
std::string get_current_username();

/**
 * @brief Gets the UID for a given username.
 * @param username Username to resolve.
 * @return UID of the user, or nullopt when the user does not exist.
 */
std::optional<uid_t> get_uid_for_user(std::string_view username);

/**
 * @brief Boosts process priority for real-time responsiveness.
 */
void boost_process_priority();

/**
 * @brief Pins process to performance cores (cores 0-3).
 */
void pin_to_pcore();

/**
 * @brief Opens input device with restricted permissions.
 * @param path Device path.
 * @param flags Open flags.
 * @param user_data Unused user data.
 * @return File descriptor or negative errno on error.
 */
int open_restricted(const char* path, int flags, void* user_data);

/**
 * @brief Closes input device.
 * @param fd File descriptor to close.
 * @param user_data Unused user data.
 */
void close_restricted(int fd, void* user_data);

/**
 * @brief Authorizes a new client of one of the server sockets.
 *
 * Fetches the peer credentials, verifies the peer UID and that the peer
 * executable is the fcitx5 binary that is allowed to talk to the server.
 *
 * @param fd Accepted client socket.
 * @param expectedUid UID the server was started for.
 * @param socketLabel Socket name used in the log messages.
 * @return Peer PID when the peer is allowed to use the socket, nullopt otherwise.
 */
std::optional<pid_t> authorizePeer(int fd, uid_t expectedUid, std::string_view socketLabel);

/**
 * @brief libinput interface callbacks.
 */
extern const struct libinput_interface interface;

/**
 * @brief Main entry point for the uinput server.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code (0 on success).
 */
int main(int argc, char* argv[]);

#endif // _LOTUS_SERVER_H_
