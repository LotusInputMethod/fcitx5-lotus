// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

    class BackspaceListener {
      public:
        BackspaceListener() {
            fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            if (fd_ < 0) {
                fail("socket");
                return;
            }
            sockaddr_un address{};
            address.sun_family    = AF_UNIX;
            const auto socketPath = buildSocketPath("kb_socket");
            address.sun_path[0]   = '\0';
            std::memcpy(&address.sun_path[1], socketPath.data(), socketPath.size());
            const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + socketPath.size() + 1);
            if (bind(fd_, reinterpret_cast<const sockaddr*>(&address), length) < 0 || listen(fd_, 1) < 0) {
                fail("bind/listen");
            }
        }

        ~BackspaceListener() {
            if (client_ >= 0)
                close(client_);
            if (fd_ >= 0)
                close(fd_);
        }

        bool receive(int& count, const char* meaning, const char* requestTimeoutExpected = "request within 2000 ms") {
            if (client_ < 0) {
                if (fd_ < 0) {
                    reportFailure("wait for replacement socket connection", "valid listener descriptor", "listener descriptor is invalid", meaning);
                    return false;
                }
                pollfd     pollfd{fd_, POLLIN, 0};
                const auto pollResult = poll(&pollfd, 1, 2000);
                if (pollResult == 0) {
                    reportFailure("wait for replacement socket connection", "connection request within 2000 ms", "poll timed out", meaning);
                    return false;
                }
                if (pollResult < 0) {
                    reportFailure("wait for replacement socket connection", "poll succeeds", "poll failed: " + std::string(std::strerror(errno)), meaning);
                    return false;
                }
                if (!(pollfd.revents & POLLIN)) {
                    reportFailure("wait for replacement socket connection", "POLLIN revents", "revents=" + std::to_string(pollfd.revents), meaning);
                    return false;
                }
                client_ = accept(fd_, nullptr, nullptr);
                if (client_ < 0) {
                    reportFailure("accept replacement socket connection", "accept succeeds", "accept failed: " + std::string(std::strerror(errno)), meaning);
                    return false;
                }
            }
            pollfd     pollfd{client_, POLLIN, 0};
            const auto pollResult = poll(&pollfd, 1, 2000);
            if (pollResult == 0) {
                reportFailure("wait for replacement request", requestTimeoutExpected, "poll timed out", meaning);
                return false;
            }
            if (pollResult < 0) {
                reportFailure("wait for replacement request", "poll succeeds", "poll failed: " + std::string(std::strerror(errno)), meaning);
                return false;
            }
            if (!(pollfd.revents & POLLIN)) {
                reportFailure("wait for replacement request", "POLLIN revents", "revents=" + std::to_string(pollfd.revents), meaning);
                return false;
            }
            const auto received = recv(client_, &count, sizeof(count), 0);
            if (received < 0) {
                reportFailure("receive replacement request", std::to_string(sizeof(count)) + " bytes", "recv failed: " + std::string(std::strerror(errno)), meaning);
                return false;
            }
            if (received != sizeof(count)) {
                reportFailure("receive replacement request", std::to_string(sizeof(count)) + " bytes", "recv returned " + std::to_string(received) + " bytes", meaning);
                return false;
            }
            return true;
        }

        bool valid() const {
            return fd_ >= 0;
        }

      private:
        void fail(const char* operation) {
            reportFailure(std::string(operation) + " replacement socket", "operation succeeds", std::string(operation) + " failed: " + std::strerror(errno),
                          "the test cannot observe Smooth replacement requests");
            close(fd_);
            fd_ = -1;
        }

        int fd_     = -1;
        int client_ = -1;
    };

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(requireAccepted),
                          "accepted=" + std::to_string(event.accepted()) + ", commits=" + std::to_string(context.commits().size()),
                          "Smooth buffered-key handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-smooth-buffered-key-replay");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs",
                      "the replay test cannot exercise Smooth Telex behavior");
        return 1;
    }

    BackspaceListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetPreeditUpdateCount();

    // Telex a, s changes the real Bamboo preedit a -> á. Smooth mode replaces
    // the old character through the kb_socket transport.
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    int backspaces = 0;
    if (!listener.receive(backspaces, "the initial Telex replacement request did not arrive"))
        return 1;
    if (backspaces <= 0) {
        reportFailure("receive initial replacement count", "backspace count > 0", "backspace count=" + std::to_string(backspaces),
                      "the Telex replacement did not request deletion of the previous character");
        return 1;
    }

    if (!send(engine, entry, *context, FcitxKey_x, true) || !context->commits().empty()) {
        reportFailure("buffer key x before deletion completes", "no immediate commits", "commits=" + std::to_string(context->commits().size()),
                      "buffered key x was emitted before the first replacement completed");
        return 1;
    }
    for (int i = 0; i < backspaces; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == backspaces))
            return 1;
    }

    int replayBackspaces = 0;
    if (!listener.receive(replayBackspaces, "buffered key was not replayed after deletion", "buffered x replay starts another replacement request within 2000 ms"))
        return 1;
    if (replayBackspaces <= 0) {
        reportFailure("receive replay replacement count", "backspace count > 0", "backspace count=" + std::to_string(replayBackspaces),
                      "buffered key x did not start another replacement after deletion");
        return 1;
    }
    for (int i = 0; i < replayBackspaces; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == replayBackspaces))
            return 1;
    }

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        std::string actual = "commits=";
        for (const auto& commit : context->commits())
            actual += "['" + commit + "']";
        const auto meaning = "the buffered key replay did not produce the expected final commits; "
                             "first-backspaces=" +
            std::to_string(backspaces) + ", replay-backspaces=" + std::to_string(replayBackspaces);
        reportFailure("verify final replay commits", "commits=['á']['ã']", actual, meaning);
        return 1;
    }
    return 0;
}
