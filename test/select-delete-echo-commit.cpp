// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file select-delete-echo-commit.cpp
 * @brief Select mode commits on the Delete echo, not on the last Left.
 *
 * Synthetic echoes, no transport involved: Telex a,s starts a Select
 * replacement (E=2). The two selection Lefts are swallowed-or-passed with
 * NO commit; the commit lands only when the Delete echo arrives (Delete is
 * sent after all Lefts, so its echo proves the app processed everything).
 *
 * RED-first: on the old logic the 2nd Left echo already commits, so the
 * "no commit yet" check below fails before the fix.
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

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        return event.accepted();
    }

    std::string commitList(const TestInputContext& context) {
        std::string out = "commits=";
        for (const auto& commit : context.commits())
            out += "['" + commit + "']";
        return out;
    }

    // Fake EIS handshake: performReplacement only runs while connected, so this
    // test accepts the addon's auth connection and hands off a real EIS fd.
    // Nothing is ever dispatched on either side: the pacer queue stays held
    // (no EventLoop running) and the echoes below are fully synthetic.
    struct FakeHandshake {
        FakeHandshake() {
            eis_ = eis_new(nullptr);
            eis_setup_backend_fd(eis_);
            eisFd_    = eis_backend_fd_add_client(eis_);
            listenFd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            sockaddr_un addr{};
            addr.sun_family  = AF_UNIX;
            addr.sun_path[0] = '\0';
            memcpy(&addr.sun_path[1], kPath.data(), kPath.size());
            const auto len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + kPath.size() + 1);
            bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), len);
            listen(listenFd_, 5);
            thread_ = std::thread([this] {
                const int client = accept(listenFd_, nullptr, nullptr);
                if (client < 0)
                    return;
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
                memcpy(CMSG_DATA(c), &eisFd_, sizeof(eisFd_));
                sendmsg(client, &msg, 0);
                close(client);
                close(listenFd_);
            });
        }

        ~FakeHandshake() {
            if (thread_.joinable())
                thread_.join();
            eis_unref(eis_);
        }

        FakeHandshake(const FakeHandshake&)            = delete;
        FakeHandshake& operator=(const FakeHandshake&) = delete;

      private:
        struct eis* eis_      = nullptr;
        int         eisFd_    = -1;
        int         listenFd_ = -1;
        std::thread thread_;
    };

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-select-delete-echo-commit");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Select)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Select) {
        check(false, "configure Select/Telex", "mode not Select");
        return 1;
    }
    // No EventLoop running: the pacer queue stays held after the push, while
    // the echo counting below runs fully synchronously. The fake handshake
    // only satisfies the connected check guarding performReplacement.
    fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);
    fcitx::KeyPacer::instance().setSocketPathForTest(kPath);
    FakeHandshake handshake;

    auto          context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    // Telex a,s -> replacement of 1 char => E = 1 + 1 trigger = 2 echoes.
    send(engine, entry, *context, FcitxKey_a);
    if (!send(engine, entry, *context, FcitxKey_s)) {
        check(false, "trigger replacement", "second key not accepted; " + commitList(*context));
        return 1;
    }

    // Echo 1st selection Left: passes through (app extends selection).
    check(!send(engine, entry, *context, FcitxKey_Left), "1st Left echo passes", commitList(*context));
    check(context->commits().empty(), "no commit after 1st Left", commitList(*context));

    // Echo 2nd (last) selection Left: swallowed, but STILL no commit.
    check(send(engine, entry, *context, FcitxKey_Left), "2nd Left echo swallowed", commitList(*context));
    check(context->commits().empty(), "no commit after last Left (held for Delete echo)", commitList(*context));

    // Echo Delete (forwarded so the app deletes first) -> commit lands now.
    check(!send(engine, entry, *context, FcitxKey_Delete), "Delete echo forwarded", commitList(*context));
    check(context->commits().size() == 1 && context->commits()[0] == "á", "exactly one commit after Delete echo", commitList(*context));

    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "select-delete-echo-commit: all checks passed\n";
    return 0;
}
