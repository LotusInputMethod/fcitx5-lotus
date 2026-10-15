// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mini-eis-fixture.h
 * @brief Shared mini EIS server fixture for headless replacement tests.
 *
 * The kb_socket transport now speaks EI behind an SCM_RIGHTS auth gate, so
 * tests can no longer observe raw KeyMsg streams. This fixture runs a real
 * (libeis) EIS server on the fcitx loop: the pacer handshakes against it
 * and paces for real, while the test asserts job shapes via
 * KeyPacer::lastPushedJobForTest() and behavior via accepted()/commits().
 * Synthetic echoes are plain key re-sends through the engine on loop
 * timers — accepted means swallowed, forwarded means passed through.
 */

#pragma once

#include "lotus-engine.h"
#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-utils/event.h>
#include <fcitx-utils/keysym.h>
#include <libeis.h>

#include <linux/input-event-codes.h>

namespace lotus_test {

    inline bool sendFd(int sock, int fd) {
        char          dummy = 0;
        struct iovec  iov{&dummy, 1};
        char          cmsgbuf[CMSG_SPACE(sizeof(int))]{};
        struct msghdr msg{};
        msg.msg_iov        = &iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = cmsgbuf;
        msg.msg_controllen = sizeof(cmsgbuf);
        struct cmsghdr* c  = CMSG_FIRSTHDR(&msg);
        c->cmsg_level      = SOL_SOCKET;
        c->cmsg_type       = SCM_RIGHTS;
        c->cmsg_len        = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd, sizeof(fd));
        return sendmsg(sock, &msg, 0) > 0;
    }

    inline void onEisReadable(struct eis* eis) {
        eis_dispatch(eis);
        struct eis_event* event = nullptr;
        while ((event = eis_get_event(eis)) != nullptr) {
            switch (eis_event_get_type(event)) {
                case EIS_EVENT_CLIENT_CONNECT: {
                    struct eis_client* client = eis_event_get_client(event);
                    eis_client_connect(client);
                    struct eis_seat* seat = eis_client_new_seat(client, "default");
                    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
                    eis_seat_add(seat);
                    eis_seat_unref(seat);
                    break;
                }
                case EIS_EVENT_SEAT_BIND: {
                    if (!eis_event_seat_has_capability(event, EIS_DEVICE_CAP_KEYBOARD))
                        break;
                    struct eis_device* dev = eis_seat_new_device(eis_event_get_seat(event));
                    eis_device_configure_name(dev, "probe-kbd");
                    eis_device_configure_capability(dev, EIS_DEVICE_CAP_KEYBOARD);
                    eis_device_add(dev);
                    eis_device_resume(dev);
                    eis_device_unref(dev);
                    break;
                }
                default: break;
            }
            eis_event_unref(event);
        }
    }

    /**
     * @brief Counts press/release taps of one key code at the head of a job.
     *
     * Skips SleepFor markers; stops at the first control item (AwaitEcho or
     * Commit tail). Fails on any unexpected KeyMsg (wrong code or broken
     * press/release order).
     */
    inline bool countJobTaps(const fcitx::KeyJob& job, uint16_t code, size_t& taps, std::string& detail) {
        taps               = 0;
        bool   expectPress = true;
        size_t keys        = 0;
        for (const auto& item : job) {
            const auto* key = std::get_if<fcitx::KeyMsg>(&item);
            if (key == nullptr) {
                if (std::get_if<fcitx::SleepFor>(&item) != nullptr)
                    continue;
                break;
            }
            ++keys;
            if (key->code != code || key->pressed != (expectPress ? 1 : 0)) {
                detail = "keys=" + std::to_string(keys) + " code=" + std::to_string(key->code);
                return false;
            }
            if (expectPress)
                ++taps;
            expectPress = !expectPress;
        }
        if (taps == 0 || !expectPress) {
            detail = "keys=" + std::to_string(keys);
            return false;
        }
        return true;
    }

    /**
     * @brief Parses a Select job head: Shift-down, N x Left taps, Shift-up,
     *        Delete tap. Returns N. There is no trailing tap: the trigger
     *        key's own Left is the final echo. Sleep markers are skipped;
     *        trailing AwaitEcho/Commit items are ignored.
     */
    inline bool parseSelectJob(const fcitx::KeyJob& job, size_t& selects, std::string& detail) {
        selects = 0;
        // Collects the leading KeyMsg run, skipping SleepFor markers; the
        // chain tail is pure control items (AwaitEcho/Commit).
        std::vector<std::pair<uint16_t, uint8_t>> keys;
        for (const auto& item : job) {
            const auto* key = std::get_if<fcitx::KeyMsg>(&item);
            if (key == nullptr) {
                if (std::get_if<fcitx::SleepFor>(&item) != nullptr)
                    continue;
                break;
            }
            keys.emplace_back(key->code, key->pressed);
        }
        size_t     i    = 0;
        const auto need = [&](uint16_t code, uint8_t pressed) { return i < keys.size() && keys[i].first == code && keys[i].second == pressed; };
        const auto fail = [&](const char* expected) {
            detail = std::string(expected) + ", keys=" + std::to_string(keys.size()) + " parsed=" + std::to_string(i);
            return false;
        };
        if (!need(KEY_LEFTSHIFT, 1))
            return fail("Shift-down first");
        ++i;
        while (i + 1 < keys.size() && keys[i].first == KEY_LEFT && keys[i + 1].first == KEY_LEFT) {
            if (keys[i].second != 1 || keys[i + 1].second != 0)
                return fail("Left press/release pairs");
            ++selects;
            i += 2;
        }
        if (selects == 0)
            return fail("at least 1 Left tap");
        if (!need(KEY_LEFTSHIFT, 0))
            return fail("Shift-up after Left taps");
        ++i;
        if (!need(KEY_DELETE, 1))
            return fail("Delete press after Shift-up");
        ++i;
        if (!need(KEY_DELETE, 0))
            return fail("Delete release after Shift-up");
        ++i;
        if (i != keys.size())
            return fail("no trailing KeyMsgs");
        return true;
    }

    struct EisFixture {
        TestInstance                          testInstance;
        std::unique_ptr<TestInputContext>     context;
        fcitx::InputMethodEntry               entry{"lotus", "Lotus", "vi", "lotus"};
        std::unique_ptr<fcitx::LotusEngine>   engine;
        struct eis*                           eis = nullptr;
        std::unique_ptr<fcitx::EventSourceIO> eisWatch;

        EisFixture() = default;
        ~EisFixture() {
            // A still-joinable helper means setup never finished (early
            // failure path): the process is exiting anyway, detach instead
            // of hanging in accept().
            if (helper_.joinable())
                helper_.detach();
            if (eis != nullptr)
                eis_unref(eis);
        }

        EisFixture(const EisFixture&)            = delete;
        EisFixture& operator=(const EisFixture&) = delete;

        bool        send(fcitx::KeySym symbol) {
            fcitx::KeyEvent event(context.get(), fcitx::Key(symbol), false);
            engine->keyEvent(entry, event);
            return event.accepted();
        }

        std::string commitList() const {
            std::string out = "commits=";
            for (const auto& commit : context->commits())
                out += "['" + commit + "']";
            return out;
        }

        // Binds the abstract socket name, spawns the fd-handoff thread and
        // points the pacer at it. The caller joins the helper once the
        // trigger keys are sent (the handoff unblocks ensureConnected).
        bool startEis(const std::string& socketName) {
            fcitx::KeyPacer::instance().setSocketPathForTest(socketName);
            eis = eis_new(nullptr);
            eis_setup_backend_fd(eis);
            const int eisFd    = eis_backend_fd_add_client(eis);
            const int listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (listenFd < 0)
                return false;
            sockaddr_un addr{};
            addr.sun_family  = AF_UNIX;
            addr.sun_path[0] = '\0';
            memcpy(&addr.sun_path[1], socketName.data(), socketName.size());
            const auto len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + socketName.size() + 1);
            if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), len) < 0 || listen(listenFd, 5) < 0) {
                close(listenFd);
                return false;
            }
            helper_    = std::thread([listenFd, eisFd] {
                const int c = accept(listenFd, nullptr, nullptr);
                if (c >= 0) {
                    sendFd(c, eisFd);
                    close(c);
                }
                close(listenFd);
            });
            auto& loop = testInstance.instance.eventLoop();
            eisWatch   = loop.addIOEvent(eis_get_fd(eis), fcitx::IOEventFlag::In, [this](fcitx::EventSourceIO*, int, fcitx::IOEventFlags) {
                onEisReadable(eis);
                return true;
            });
            onEisReadable(eis);
            return true;
        }

        void joinHelper() {
            if (helper_.joinable())
                helper_.join();
        }

        template <typename Fn>
        void at(std::chrono::milliseconds delay, Fn&& fn) {
            auto& loop = testInstance.instance.eventLoop();
            // Relative to the call time: a call inside a timer callback
            // schedules delay after THAT callback runs, not after test start.
            const uint64_t when = fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(delay).count());
            // Timers die with their handle: retain every step until exec ends.
            timers_.push_back(loop.addTimeEvent(CLOCK_MONOTONIC, when, 1000, [fn = std::forward<Fn>(fn)](fcitx::EventSourceTime*, uint64_t) {
                fn();
                return false;
            }));
        }

        void quitAt(std::chrono::milliseconds delay) {
            at(delay, [this] { testInstance.instance.eventLoop().exit(); });
        }

      private:
        std::thread                                          helper_;
        std::vector<std::unique_ptr<fcitx::EventSourceTime>> timers_;
    };

} // namespace lotus_test
