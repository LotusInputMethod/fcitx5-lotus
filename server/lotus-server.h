/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _LOTUS_SERVER_H_
#define _LOTUS_SERVER_H_

#include <atomic>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/uinput.h>
#include <poll.h>
#include <pwd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

class FdGuard {
  public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}
    ~FdGuard();

    FdGuard(const FdGuard&)            = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    FdGuard(FdGuard&& other) noexcept;
    FdGuard& operator=(FdGuard&& other) noexcept;

    int  get()      const { return fd_; }
    bool is_valid() const { return fd_ >= 0; }
    void reset(int new_fd = -1);

  private:
    int fd_;
};

class UinputDevice {
  public:
    UinputDevice() = default;
    ~UinputDevice();

    UinputDevice(const UinputDevice&)            = delete;
    UinputDevice& operator=(const UinputDevice&) = delete;
    UinputDevice(UinputDevice&&)                 = default;
    UinputDevice& operator=(UinputDevice&&)      = default;

    bool initialize();
    void send_backspace();
    int  get_fd() const { return guard_.get(); }

  private:
    FdGuard guard_;
};

class LibinputContext {
  public:
    explicit LibinputContext(const struct libinput_interface* interface);
    ~LibinputContext();

    LibinputContext(const LibinputContext&)            = delete;
    LibinputContext& operator=(const LibinputContext&) = delete;
    LibinputContext(LibinputContext&& other) noexcept : udev_(other.udev_), li_(other.li_) {
        other.udev_ = nullptr;
        other.li_   = nullptr;
    }
    LibinputContext& operator=(LibinputContext&& other) noexcept {
        if (this != &other) {
            this->~LibinputContext();
            udev_ = other.udev_; li_ = other.li_;
            other.udev_ = nullptr; other.li_ = nullptr;
        }
        return *this;
    }

    bool             is_valid() const { return li_ != nullptr; }
    struct libinput* get_li()   const { return li_; }
    int              get_fd()   const { return libinput_get_fd(li_); }

  private:
    struct udev*     udev_ = nullptr;
    struct libinput* li_   = nullptr;
};

#define UNIX_PATH_MAX sizeof(((struct sockaddr_un*)0)->sun_path)

extern std::atomic<bool> g_running; //NOLINT

void    signal_handler(int sig);
bool    get_current_username(char* out, size_t len);
uid_t   get_uid_for_user(const char* username);
void    boost_process_priority();
void    pin_to_pcore();
int     open_restricted(const char* path, int flags, void* user_data);
void    close_restricted(int fd, void* user_data);

extern const struct libinput_interface interface;

int main(int argc, char* argv[]);

#endif // _LOTUS_SERVER_H_
