/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-fd-guard.h
 * @brief RAII wrapper for Unix file descriptors, shared by the addon and the uinput server.
 *
 * Header-only because the addon and the server are two separate targets: the server includes it
 * relatively (like lotus-protocol.h) and does not link the addon core.
 */

#ifndef _LOTUS_FD_GUARD_H_
#define _LOTUS_FD_GUARD_H_

#include <unistd.h>

/**
 * @brief RAII Wrapper for Unix File Descriptors.
 * Ensures that close() is called when the object goes out of scope.
 * Complies with Rule of Five by disabling copy and implementing move.
 */
class FdGuard {
  public:
    /**
     * @brief Takes ownership of a descriptor.
     * @param fd Descriptor to own, -1 when nothing is owned.
     */
    explicit FdGuard(int fd = -1) : fd_(fd) {}

    /**
     * @brief Closes the owned descriptor, if any.
     */
    ~FdGuard() {
        reset();
    }

    // Rule of Five: Disable copying, allow moving
    FdGuard(const FdGuard&)            = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    /**
     * @brief Takes the descriptor of another guard, which stops owning it.
     * @param other Guard to move from.
     */
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    /**
     * @brief Closes the owned descriptor and takes the descriptor of another guard.
     * @param other Guard to move from.
     * @return This guard.
     */
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            reset(other.fd_);
            other.fd_ = -1;
        }
        return *this;
    }

    /**
     * @brief Reads the owned descriptor.
     * @return Descriptor, or -1 when nothing is owned.
     */
    [[nodiscard]] int get() const {
        return fd_;
    }

    /**
     * @brief Reports whether a descriptor is owned.
     * @return True when the guard owns a descriptor.
     */
    [[nodiscard]] bool is_valid() const {
        return fd_ >= 0;
    }

    /**
     * @brief Closes the owned descriptor and takes a new one.
     * @param new_fd Descriptor to own, -1 to own nothing.
     */
    void reset(int new_fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = new_fd;
    }

    /**
     * @brief Relinquishes ownership of the descriptor without closing it.
     *
     * For the hand-off of a descriptor to the global socket variables the addon stores in an
     * atomic: the caller then owns the descriptor and must close it.
     * @return The descriptor, or -1 when the guard owns nothing.
     */
    [[nodiscard]] int release() {
        const int released = fd_;
        fd_                = -1;
        return released;
    }

  private:
    int fd_;
};

#endif // _LOTUS_FD_GUARD_H_
