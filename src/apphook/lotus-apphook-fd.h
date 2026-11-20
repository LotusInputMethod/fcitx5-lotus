/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _SRC_LOTUS_APPHOOK_FD_H_
#define _SRC_LOTUS_APPHOOK_FD_H_

#include <cerrno>
#include <unistd.h>
#include <utility>

namespace lotus::apphook {

    /**
     * @brief Repeats a system call while a signal interrupts it.
     * @param call Callable returning a negative value and setting errno on failure.
     * @return Result of the first call not failing with EINTR.
     */
    template <typename Call>
    auto retryOnEintr(const Call& call) {
        auto result = call();
        while (result < 0 && errno == EINTR)
            result = call();
        return result;
    }

    /**
     * @brief Tests poll() result bits without signed bitwise arithmetic.
     * @param revents pollfd::revents.
     * @param events POLL* flags to test.
     * @return Whether any of the flags is set.
     */
    inline bool hasPollEvents(short revents, int events) {
        return (static_cast<unsigned>(revents) & static_cast<unsigned>(events)) != 0U;
    }

    class OwnedFd {
      public:
        OwnedFd() = default;
        explicit OwnedFd(int fd) : fd_(fd) {}
        ~OwnedFd() {
            reset();
        }
        OwnedFd(const OwnedFd&)            = delete;
        OwnedFd& operator=(const OwnedFd&) = delete;
        OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
        OwnedFd& operator=(OwnedFd&& other) noexcept {
            if (this != &other)
                reset(std::exchange(other.fd_, -1));
            return *this;
        }
        [[nodiscard]] int get() const {
            return fd_;
        }
        void reset(int fd = -1) {
            if (fd == fd_)
                return;
            if (fd_ >= 0)
                close(fd_);
            fd_ = fd;
        }

      private:
        int fd_ = -1;
    };

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_FD_H_
