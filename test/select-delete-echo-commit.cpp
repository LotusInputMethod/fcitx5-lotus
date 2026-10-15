// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file select-delete-echo-commit.cpp
 * @brief Select mode commits via the chain barrier, not early.
 *
 * Telex a,s starts a Select replacement (E=2). A mini EIS server runs on the
 * fcitx loop (single exec, exit from a timer): the pacer sends for
 * real, parks at AwaitEcho{3}, and timer-driven synthetic echoes drive the
 * count: Left (pass), Left (swallowed, still no commit), Delete (forwarded)
 * -> barrier lifts -> exactly one `á` commit. No wall-clock asserts, only
 * order; generous timer margins keep it deterministic.
 */

#include "lotus-engine.h"
#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-utils/event.h>
#include <fcitx-utils/keysym.h>
#include <libeis.h>

namespace {

    const std::string kPath("lotus-test-delete-echo-kb");

    int               failures = 0;

    void              check(bool ok, const std::string& step, const std::string& detail) {
        if (ok)
            return;
        ++failures;
        std::cerr << "FAIL: " << step << " -- " << detail << "\n";
    }

    struct Fixture {
        TestInstance                      testInstance;
        std::unique_ptr<TestInputContext> context;
        fcitx::InputMethodEntry           entry{"lotus", "Lotus", "vi", "lotus"};
        fcitx::LotusEngine*               engine = nullptr;
        struct eis*                       eis    = nullptr;

        bool                              send(fcitx::KeySym symbol) {
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
    };

    static bool sendFd(int sock, int fd) {
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

    static void onEisReadable(struct eis* eis) {
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

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-delete-echo-commit");
    Fixture fix;
    fix.engine = new fcitx::LotusEngine(&fix.testInstance.instance);
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    fix.engine->setConfig(config);
    if (fix.engine->config().mode.value() != fcitx::LotusMode::Select) {
        check(false, "configure Select/Telex", "mode not Select");
        return 1;
    }
    fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);
    fcitx::KeyPacer::instance().setSocketPathForTest(kPath);

    // Mini EIS server: real handshake + dispatch on the loop; no uinput.
    fix.eis = eis_new(nullptr);
    eis_setup_backend_fd(fix.eis);
    const int   eisFd    = eis_backend_fd_add_client(fix.eis);
    const int   listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family  = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], kPath.data(), kPath.size());
    const auto len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + kPath.size() + 1);
    bind(listenFd, reinterpret_cast<sockaddr*>(&addr), len);
    listen(listenFd, 5);
    // Helper thread: accept + hand off the EIS fd (unblocks ensureConnected).
    std::thread helper([listenFd, eisFd] {
        const int c = accept(listenFd, nullptr, nullptr);
        if (c >= 0) {
            sendFd(c, eisFd);
            close(c);
        }
        close(listenFd);
    });

    fix.context = std::make_unique<TestInputContext>(&fix.testInstance.instance);
    fix.context->focusIn();
    fcitx::InputContextEvent focus(fix.context.get(), fcitx::EventType::InputContextFocusIn);
    fix.engine->activate(fix.entry, focus);
    fix.context->resetPreeditUpdateCount();

    // Telex a,s -> replacement of 1 char => E = 1 + 1 trigger = 2 echoes.
    // The keyEvents run before exec(); the handshake fd arrives via helper.
    fix.send(FcitxKey_a);
    if (!fix.send(FcitxKey_s)) {
        check(false, "trigger replacement", "second key not accepted; " + fix.commitList());
        return 1;
    }
    helper.join();

    auto& loop = fix.testInstance.instance.eventLoop();
    auto  io   = loop.addIOEvent(eis_get_fd(fix.eis), fcitx::IOEventFlag::In, [&fix](fcitx::EventSourceIO*, int, fcitx::IOEventFlags) {
        onEisReadable(fix.eis);
        return true;
    });
    onEisReadable(fix.eis);
    auto at = [&](uint64_t ms, auto fn) {
        const uint64_t when = fcitx::now(CLOCK_MONOTONIC) + ms * 1000ULL;
        return loop.addTimeEvent(CLOCK_MONOTONIC, when, 1000, [fn](fcitx::EventSourceTime*, uint64_t) {
            fn();
            return false;
        });
    };
    // Synthetic echoes on the loop thread (same thread as prod keyEvents).
    // By +300ms the handshake (~ms) and paced burst are long done.
    auto t1   = at(300, [&fix] {
        check(!fix.send(FcitxKey_Left), "1st Left echo passes", fix.commitList());
        check(fix.context->commits().empty(), "no commit after 1st Left", fix.commitList());
    });
    auto t2   = at(600, [&fix] {
        check(fix.send(FcitxKey_Left), "2nd Left echo swallowed", fix.commitList());
        check(fix.context->commits().empty(), "no commit after last Left (barrier holds)", fix.commitList());
    });
    auto t3   = at(900, [&fix] {
        check(!fix.send(FcitxKey_Delete), "Delete echo forwarded", fix.commitList());
        check(fix.context->commits().size() == 1 && fix.context->commits()[0] == "á", "exactly one commit after Delete echo", fix.commitList());
    });
    auto quit = at(3000, [&fix] { fix.testInstance.instance.eventLoop().exit(); });
    (void)io;
    (void)t1;
    (void)t2;
    (void)t3;
    (void)quit;
    loop.exec();

    eis_unref(fix.eis);
    delete fix.engine;
    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "select-delete-echo-commit: all checks passed\n";
    return 0;
}
