/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-virtual-keyboard.h"
#include "lotus-utils.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <libei.h>
#include <fcitx-utils/event.h>

namespace fcitx {

    namespace {

        // Upper bound for waiting on the server's fd handoff (local socket: instant
        // in practice; only a hung server hits this).
        constexpr int kHandshakeTimeoutMs = 2000;

        // Auth socket name: explicit test override > $LOTUS_EIS_SOCKET >
        // buildSocketPath("kb_socket"). Abstract namespace (stored without the
        // leading NUL; callers set sun_path[0] = '\0').
        std::string resolveSocketName(const std::string& overridePath) {
            if (!overridePath.empty())
                return overridePath;
            if (const char* env = getenv("LOTUS_EIS_SOCKET"); env != nullptr && *env != '\0')
                return env;
            return buildSocketPath("kb_socket");
        }

        // Receives a single fd via SCM_RIGHTS. Returns the fd, or -1.
        int recvFd(int sock) {
            char          dummy = 0;
            struct iovec  iov{&dummy, 1};
            char          cmsgbuf[CMSG_SPACE(sizeof(int))]{};
            struct msghdr msg{};
            msg.msg_iov        = &iov;
            msg.msg_iovlen     = 1;
            msg.msg_control    = cmsgbuf;
            msg.msg_controllen = sizeof(cmsgbuf);
            if (recvmsg(sock, &msg, 0) <= 0)
                return -1;
            for (struct cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
                if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS && c->cmsg_len >= CMSG_LEN(sizeof(int))) {
                    int fd = -1;
                    memcpy(&fd, CMSG_DATA(c), sizeof(fd));
                    return fd;
                }
            }
            return -1;
        }

    } // namespace

    VirtualKeyboard::~VirtualKeyboard() {
        close();
    }

    void VirtualKeyboard::init(EventLoop* loop) {
        std::scoped_lock lock(m_);
        loop_ = loop;
    }

    void VirtualKeyboard::setResumeCallback(std::function<void()> callback) {
        std::scoped_lock lock(m_);
        resumeCallback_ = std::move(callback);
    }

    void VirtualKeyboard::setSocketPath(std::string path) {
        std::scoped_lock lock(m_);
        if (path_ != path) {
            path_ = std::move(path);
            teardownEiLocked();
        }
    }

    bool VirtualKeyboard::ensureConnected() {
        std::scoped_lock lock(m_);
        if (ready_)
            return true;
        if (ei_ != nullptr)
            return true; // handshake already in flight; RESUMED flips ready_
        if (loop_ == nullptr)
            return false; // no event loop to dispatch EIS on
        teardownEiLocked();

        const std::string name = resolveSocketName(path_);
        const int         auth = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (auth < 0)
            return false;

        bool ok = false;
        do {
            struct sockaddr_un addr{};
            addr.sun_family      = AF_UNIX;
            const size_t copyLen = std::min(name.size(), sizeof(addr.sun_path) - 2);
            addr.sun_path[0]     = '\0';
            memcpy(&addr.sun_path[1], name.data(), copyLen);
            const auto len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + copyLen + 1);
            if (connect(auth, reinterpret_cast<struct sockaddr*>(&addr), len) != 0)
                break;
            struct pollfd pfd{auth, POLLIN, 0};
            if (poll(&pfd, 1, kHandshakeTimeoutMs) <= 0)
                break;
            const int eisFd = recvFd(auth);
            if (eisFd < 0)
                break;
            ei_ = ei_new_sender(nullptr);
            if (ei_ == nullptr) {
                ::close(eisFd);
                break;
            }
            ei_configure_name(ei_, "fcitx5-lotus");
            if (ei_setup_backend_fd(ei_, eisFd) != 0) {
                ::close(eisFd); // setup refused ownership
                ei_unref(ei_);
                ei_ = nullptr;
                break;
            }
            // ei owns eisFd from here on.
            io_ = loop_->addIOEvent(ei_get_fd(ei_), IOEventFlag::In, [this](EventSourceIO*, int, IOEventFlags) {
                onEiReadable();
                return true;
            });
            ok  = true;
        } while (false);
        ::close(auth);
        if (!ok)
            teardownEiLocked();
        return ok;
    }

    bool VirtualKeyboard::send(uint16_t code, uint8_t pressed) {
        std::scoped_lock lock(m_);
        if (!ready_ || kbd_ == nullptr || ei_ == nullptr)
            return false;
        ei_device_keyboard_key(kbd_, code, pressed != 0);
        ei_device_frame(kbd_, ei_now(ei_));
        return true;
    }

    bool VirtualKeyboard::isReady() const {
        std::scoped_lock lock(m_);
        return ready_;
    }

    void VirtualKeyboard::close() {
        std::scoped_lock lock(m_);
        teardownEiLocked();
    }

    void VirtualKeyboard::onEiReadable() {
        std::function<void()> resume;
        {
            std::scoped_lock lock(m_);
            if (ei_ == nullptr)
                return;
            ei_dispatch(ei_);
            struct ei_event* event = nullptr;
            while (ei_ != nullptr && (event = ei_get_event(ei_)) != nullptr) {
                if (handleEiEventLocked(event))
                    resume = resumeCallback_;
                ei_event_unref(event);
            }
        }
        if (resume)
            resume();
    }

    bool VirtualKeyboard::handleEiEventLocked(struct ei_event* event) {
        switch (ei_event_get_type(event)) {
            case EI_EVENT_SEAT_ADDED:
                if (seat_ == nullptr) {
                    seat_ = ei_seat_ref(ei_event_get_seat(event));
                    ei_seat_bind_capabilities(seat_, EI_DEVICE_CAP_KEYBOARD, NULL);
                }
                break;
            case EI_EVENT_SEAT_REMOVED:
                if (seat_ != nullptr && ei_event_get_seat(event) == seat_) {
                    ei_seat_unref(seat_);
                    seat_  = nullptr;
                    ready_ = false;
                }
                break;
            case EI_EVENT_DEVICE_ADDED:
                if (kbd_ == nullptr && ei_device_has_capability(ei_event_get_device(event), EI_DEVICE_CAP_KEYBOARD))
                    kbd_ = ei_device_ref(ei_event_get_device(event));
                break;
            case EI_EVENT_DEVICE_REMOVED:
                if (kbd_ != nullptr && ei_event_get_device(event) == kbd_) {
                    ei_device_unref(kbd_);
                    kbd_   = nullptr;
                    ready_ = false;
                }
                break;
            case EI_EVENT_DEVICE_RESUMED:
                if (kbd_ != nullptr && ei_event_get_device(event) == kbd_) {
                    ei_device_start_emulating(kbd_, ++seq_);
                    if (!ready_) {
                        ready_ = true;
                        return true;
                    }
                }
                break;
            case EI_EVENT_DEVICE_PAUSED:
                if (kbd_ != nullptr && ei_event_get_device(event) == kbd_)
                    ready_ = false;
                break;
            case EI_EVENT_DEVICE_STOP_EMULATING: ready_ = false; break;
            case EI_EVENT_DISCONNECT: teardownEiLocked(); break;
            default: break;
        }
        return false;
    }

    void VirtualKeyboard::teardownEiLocked() {
        io_.reset();
        if (kbd_ != nullptr) {
            ei_device_unref(kbd_);
            kbd_ = nullptr;
        }
        if (seat_ != nullptr) {
            ei_seat_unref(seat_);
            seat_ = nullptr;
        }
        if (ei_ != nullptr) {
            ei_unref(ei_); // closes the owned EIS fd
            ei_ = nullptr;
        }
        ready_ = false;
    }

} // namespace fcitx
