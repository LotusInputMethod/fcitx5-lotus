// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file runtime-safety.cpp
 * @brief Headless integration test: the values and descriptors the engine creates while it runs
 *        are released on every path.
 *
 * Covers the three lifetimes the pointer/RAII audit touched: the $TIME/$DATE local time (a stack
 * std::tm instead of a pointer into the shared libc buffer), the descriptors of the EIS transport
 * (the ei/eis socketpair the addon keeps for its lifetime) and the pipe of the dark-mode probe
 * (unique_ptr with pclose as deleter). Word replacement in Smooth mode must not leak a descriptor,
 * however many times it replaces.
 *
 * The transport runs against a mini EIS server on a private socket name (setSocketPathForTest):
 * a helper thread accepts the addon's connection and hands off the EIS fd, so the test is
 * self-contained and never touches the kb_socket of the session.
 */

#include "kb-socket-listener.h"
#include "key_processor.h"
#include "lotus-engine.h"
#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx/event.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include <libeis.h>

namespace {

    // The engine carries fcitx5's raw modifier bits through the C boundary, so every bit the
    // portable model uses must stay identical to fcitx5's KeyState.
    static_assert(static_cast<uint32_t>(lotus::KeyState::Shift) == static_cast<uint32_t>(fcitx::KeyState::Shift), "Shift bit");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Lock) == static_cast<uint32_t>(fcitx::KeyState::CapsLock), "Lock bit is CapsLock");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Ctrl) == static_cast<uint32_t>(fcitx::KeyState::Ctrl), "Ctrl bit");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Mod1) == static_cast<uint32_t>(fcitx::KeyState::Alt), "Mod1 bit is Alt");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Ignored) == static_cast<uint32_t>(fcitx::KeyState::IgnoredMask), "Ignored bit is IgnoredMask");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Super) == static_cast<uint32_t>(fcitx::KeyState::Super2), "Super bit is Super2");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Hyper) == static_cast<uint32_t>(fcitx::KeyState::Hyper2), "Hyper bit is Hyper2");
    static_assert(static_cast<uint32_t>(lotus::KeyState::Meta) == static_cast<uint32_t>(fcitx::KeyState::Meta), "Meta bit");

    // Number of descriptors this process currently has open.
    std::size_t openDescriptorCount() {
        std::size_t     count = 0;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", error)) {
            (void)entry;
            ++count;
        }
        if (error) {
            reportFailure("count open descriptors", "readable /proc/self/fd", error.message(), "the descriptor leak check needs /proc/self/fd");
        }
        return count;
    }

    // Renders the current hour through the $TIME macro under the given zone.
    std::string hourInZone(const char* zone) {
        setenv("TZ", zone, 1);
        tzset();
        return lotus::KeyProcessor::formatTime("15");
    }

    // Sends one descriptor over a connected socket (SCM_RIGHTS), the handoff the real
    // server performs for every authenticated client of the kb_socket.
    bool sendFd(int sock, int fd) {
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

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-runtime-safety");
    bool ok = true;

    // Step 1: $TIME follows TZ, so the local time is still read from the zone of the process and
    // not from a fixed or shared time.
    const std::string hourAhead = hourInZone("UTC-7");
    const std::string hourUtc   = hourInZone("UTC0");
    if (hourAhead.size() != 2 || hourUtc.size() != 2) {
        reportFailure("$TIME hour", "two digits in both zones", "[" + hourAhead + "] and [" + hourUtc + "]", "$TIME must render the hour of the configured zone with two digits");
        ok = false;
    } else {
        const int difference = ((std::stoi(hourAhead) - std::stoi(hourUtc)) + 24) % 24;
        if (difference != 7) {
            reportFailure("$TIME hour", "UTC-7 (local time UTC+7) ahead of UTC0 by 7 hours", "difference=" + std::to_string(difference),
                          "$TIME must render the local time of the configured TZ, not UTC or a fixed zone");
            ok = false;
        }
    }

    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs",
                      "the descriptor check needs the transport of the Smooth mode");
        return 1;
    }

    // Mini EIS server on a private socket name: the addon under test connects here and the
    // helper thread hands off the EIS fd, so the handshake completes without the session's
    // kb_socket and the descriptor check covers the transport the addon keeps for life.
    const std::string kPath("lotus-test-runtime-safety-kb");
    fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);
    fcitx::KeyPacer::instance().setSocketPathForTest(kPath);

    struct eis* eis = eis_new(nullptr);
    eis_setup_backend_fd(eis);
    const int   eisFd = eis_backend_fd_add_client(eis);

    const int   listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family  = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], kPath.data(), kPath.size());
    const auto len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + kPath.size() + 1);
    if (listenFd < 0 || bind(listenFd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0 || listen(listenFd, 5) != 0) {
        reportFailure("bind the private EIS socket", "bind/listen succeeds", "bind/listen failed", "the descriptor check needs the transport of the Smooth mode");
        return 1;
    }
    // Helper thread: accept + hand off the EIS fd (unblocks ensureConnected).
    std::thread helper([listenFd, eisFd] {
        const int c = accept(listenFd, nullptr, nullptr);
        if (c >= 0) {
            sendFd(c, eisFd);
            close(c);
        }
        close(listenFd);
    });

    auto        context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    // Step 2: replacing a word in Smooth mode sends the request over the socket. The first word
    // establishes the connection the addon keeps for the rest of its life, so it is sent before the
    // baseline is taken; every later replacement must leave the descriptor count unchanged.
    for (const fcitx::KeySym symbol : {FcitxKey_a, FcitxKey_s}) {
        fcitx::KeyEvent event(context.get(), fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
    }
    helper.join();
    const std::size_t before = openDescriptorCount();
    for (int word = 0; word < 25; ++word) {
        for (const fcitx::KeySym symbol : {FcitxKey_a, FcitxKey_s}) {
            fcitx::KeyEvent event(context.get(), fcitx::Key(symbol), false);
            engine.keyEvent(entry, event);
        }
    }
    const std::size_t afterWords = openDescriptorCount();
    if (afterWords != before) {
        reportFailure("descriptor count over 50 keys", std::to_string(before) + " open descriptors", std::to_string(afterWords) + " open descriptors",
                      "the kb_socket transport must not leak a descriptor while it replaces words");
        ok = false;
    }

    // Step 3: the icon of the status area probes the dark mode with popen, whose pipe must be
    // closed. The default "Icon Color = Auto" configuration reaches the probe.
    (void)engine.subModeIcon(entry, *context);
    const std::size_t afterProbe = openDescriptorCount();
    if (afterProbe != before) {
        reportFailure("descriptor count over the theme probe", std::to_string(before) + " open descriptors", std::to_string(afterProbe) + " open descriptors",
                      "the dark-mode probe must close the pipe of the subprocess it spawns");
        ok = false;
    }

    if (ok) {
        std::cout << "runtime safety: all checks passed (open descriptors " << before << ")\n";
    }
    eis_unref(eis);
    return ok ? 0 : 1;
}
