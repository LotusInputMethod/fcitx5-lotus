/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window_focus_tracker.h"
#include "lotus-dbus.h"
#include "lotus-utils.h"

#include <chrono>
#include <utility>

namespace fcitx {
    namespace {
        constexpr int kPokeTimeoutMs      = 2000; ///< Registry RegisterEvent
        constexpr int kReadWriteTimeoutMs = 50;   ///< Signal poll interval
        constexpr int kReconnectDelayMs   = 100;  ///< Delay between reconnect attempts
        constexpr int kReconnectAttempts  = 20;   ///< Attempts before the bus is retried
        constexpr int kRedialAttempts     = 10;   ///< Attempts after a bus loss
    } // namespace

    WindowFocusTracker::WindowFocusTracker(std::function<void()> focusChanged) : focusChanged_(std::move(focusChanged)) {}

    bool WindowFocusTracker::registerEvent(const DbusConnectionPtr& bus, const char* eventName) {
        return dbusCallMethod(bus.get(), "org.a11y.atspi.Registry", "/org/a11y/atspi/registry", "org.a11y.atspi.Registry", "RegisterEvent", eventName, kPokeTimeoutMs) != nullptr;
    }

    bool WindowFocusTracker::addMatch(const DbusConnectionPtr& bus, const char* rule) {
        DbusErrorGuard error;
        dbus_bus_add_match(bus.get(), rule, error.get());
        return !error.isSet();
    }

    bool WindowFocusTracker::registerFocusEvents(const DbusConnectionPtr& bus) {
        return registerEvent(bus, "window:activate") && registerEvent(bus, "window:deactivate") && registerEvent(bus, "window:destroy") &&
            addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Window'") &&
            addMatch(bus, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'");
    }

    std::optional<bool> WindowFocusTracker::windowState(const DbusMessagePtr& message) {
        if (dbus_message_is_signal(message.get(), "org.a11y.atspi.Event.Window", "Activate") != FALSE) {
            return true;
        }
        if (dbus_message_is_signal(message.get(), "org.a11y.atspi.Event.Window", "Deactivate") != FALSE ||
            dbus_message_is_signal(message.get(), "org.a11y.atspi.Event.Window", "Destroy") != FALSE) {
            return false;
        }
        return std::nullopt;
    }

    std::optional<std::string> WindowFocusTracker::focusedProgram() const {
        std::scoped_lock lock(programMutex_);
        if (focusedProgram_.empty()) {
            return std::nullopt;
        }
        return focusedProgram_;
    }

    void WindowFocusTracker::updateFocusedProgram(std::string_view program) {
        {
            std::scoped_lock lock(programMutex_);
            if (focusedProgram_ == program) {
                return;
            }
            focusedProgram_ = program;
        }
        if (focusChanged_) {
            focusChanged_();
        }
    }

    WindowFocusTracker::~WindowFocusTracker() {
        stop();
    }

    void WindowFocusTracker::start() {
        if (thread_.joinable()) {
            return;
        }
        stopRequested_.store(false, std::memory_order_release);
        thread_ = std::thread(&WindowFocusTracker::threadFunc, this);
    }

    void WindowFocusTracker::stop() {
        stopRequested_.store(true, std::memory_order_release);
        updateFocusedProgram({});
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void WindowFocusTracker::threadFunc() {
        dbusEnsureThreadsInitialised();
        while (!stopRequested_.load(std::memory_order_acquire)) {
            DbusConnectionPtr bus = dbusConnectAtspiBus();
            if (!bus || !registerFocusEvents(bus)) {
                updateFocusedProgram({});
                for (int retry = 0; retry < kReconnectAttempts && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
                }
                continue;
            }
            std::string windowBus;
            std::string windowPath;
            std::string windowProgram;
            auto        clearWindowFocus = [&] {
                windowBus.clear();
                windowPath.clear();
                windowProgram.clear();
                updateFocusedProgram({});
            };
            while (!stopRequested_.load(std::memory_order_acquire) && dbus_connection_read_write(bus.get(), kReadWriteTimeoutMs) != FALSE) {
                while (DbusMessagePtr message{dbus_connection_pop_message(bus.get())}) {
                    if (dbus_message_is_signal(message.get(), "org.freedesktop.DBus", "NameOwnerChanged") != FALSE) {
                        const char*    name     = nullptr;
                        const char*    oldOwner = nullptr;
                        const char*    newOwner = nullptr;
                        DbusErrorGuard error;
                        if (dbus_message_get_args(message.get(), error.get(), DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &oldOwner, DBUS_TYPE_STRING, &newOwner,
                                                  DBUS_TYPE_INVALID) != FALSE &&
                            name != nullptr && newOwner != nullptr && *newOwner == '\0' && name == windowBus) {
                            clearWindowFocus();
                        }
                    }
                    if (const auto active = windowState(message)) {
                        const char* sender = dbus_message_get_sender(message.get());
                        const char* path   = dbus_message_get_path(message.get());
                        if (sender != nullptr && path != nullptr) {
                            if (*active) {
                                // Window activation is the authority for the app behind a shared GNOME input context.
                                windowBus     = sender;
                                windowPath    = path;
                                windowProgram = programForPid(dbusConnectionPid(bus.get(), sender));
                                updateFocusedProgram(windowProgram);
                            } else if (windowBus == sender && windowPath == path) {
                                clearWindowFocus();
                            }
                        }
                    }
                }
            }
            updateFocusedProgram({});
            for (int retry = 0; retry < kRedialAttempts && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
            }
        }
    }
} // namespace fcitx
