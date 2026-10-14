// SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file direct-eis-socket.cpp
 * @brief A filesystem socket path (leading '/') selects the direct EI mode.
 *
 * A path starting with '/' names a plain EI socket (like reis-based daemons,
 * e.g. eis-wlroots): the addon connects and speaks the EI protocol directly,
 * no SCM_RIGHTS auth handshake. A mini libeis server listens on an absolute
 * path; the pacer must deliver a backspace tap to it over the loop.
 */

#include "lotus-key-pacer.h"
#include "test-input-context.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include <linux/input-event-codes.h>
#include <fcitx-utils/event.h>
#include <libeis.h>

namespace {

    int       keyEventsSeen = 0;
    int       backspacePresses = 0;
    int       failures = 0;

    void      check(bool ok, const std::string& what) {
        if (ok)
            return;
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }

    // Mini EIS server (libeis socket backend): accept + handshake + device,
    // then count keyboard keys arriving from the addon.
    void onEisReadable(struct eis* eis) {
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
                    eis_device_configure_name(dev, "direct-kbd");
                    eis_device_configure_capability(dev, EIS_DEVICE_CAP_KEYBOARD);
                    eis_device_add(dev);
                    eis_device_resume(dev);
                    eis_device_unref(dev);
                    break;
                }
                case EIS_EVENT_KEYBOARD_KEY: {
                    ++keyEventsSeen;
                    if (eis_event_keyboard_get_key(event) == KEY_BACKSPACE && eis_event_keyboard_get_key_is_press(event))
                        ++backspacePresses;
                    break;
                }
                default: break;
            }
            eis_event_unref(event);
        }
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-direct-eis-socket");
    // Absolute filesystem path: must select the direct EI mode, not the
    // abstract auth-gate handshake.
    const std::string socketPath =
        (std::filesystem::temp_directory_path() / "fcitx5-lotus-direct-eis-socket" / "direct-eis.sock").string();

    TestInstance testInstance;
    fcitx::KeyPacer::instance().init(&testInstance.instance);
    fcitx::KeyPacer::instance().setSocketPathForTest(socketPath);
    fcitx::KeyPacer::instance().setImmediateDeliveryForTest(true);

    // Mini EIS server on the filesystem socket (reis Listener::bind twin).
    struct eis* eis = eis_new(nullptr);
    check(eis != nullptr, "eis_new");
    check(eis_setup_backend_socket(eis, socketPath.c_str()) == 0, "eis_setup_backend_socket on absolute path");

    auto& loop = testInstance.instance.eventLoop();
    auto  io   = loop.addIOEvent(eis_get_fd(eis), fcitx::IOEventFlag::In, [eis](fcitx::EventSourceIO*, int, fcitx::IOEventFlags) {
        onEisReadable(eis);
        return true;
    });
    (void)io;
    onEisReadable(eis);

    // One backspace tap: parks while the device is not ready, delivers once
    // RESUMED arrives over the direct socket.
    fcitx::KeyPacer::instance().push(fcitx::buildBackspaceJob(1, std::chrono::milliseconds(0), std::chrono::milliseconds(1), std::chrono::milliseconds(1)));

    // Generous exit: a working transport delivers within ms; 3s covers CI.
    const uint64_t when = fcitx::now(CLOCK_MONOTONIC) + 3000ULL * 1000;
    auto quit = loop.addTimeEvent(CLOCK_MONOTONIC, when, 1000, [&loop](fcitx::EventSourceTime*, uint64_t) {
        loop.exit();
        return false;
    });
    (void)quit;
    loop.exec();

    check(keyEventsSeen >= 2, "server received backspace press+release over the direct socket");
    check(backspacePresses == 1, "exactly one KEY_BACKSPACE press reached the server");

    eis_unref(eis);
    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "direct-eis-socket: all checks passed\n";
    return 0;
}
