/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef _SRC_LOTUS_APPHOOK_FD_H_
#define _SRC_LOTUS_APPHOOK_FD_H_

#include <unistd.h>
#include <utility>

namespace lotus::apphook {

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
            if (fd_ >= 0)
                close(fd_);
            fd_ = fd;
        }

      private:
        int fd_ = -1;
    };

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_FD_H_
