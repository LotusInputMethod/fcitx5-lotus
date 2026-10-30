/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window_focus_tracker.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <ranges>
#include <thread>

#include <dbus/dbus.h>
#include <unistd.h>

namespace fcitx {
    namespace {
        constexpr int kMaxAccessibleNodes = 256;

        bool          isChromiumProgram(const std::string& program) {
            constexpr std::array<std::string_view, 11> names = {"chrome", "chromium", "brave", "edge", "vivaldi", "opera", "cromite", "helium", "thorium", "slimjet", "yandex"};
            return std::ranges::any_of(names, [&program](std::string_view name) { return program.find(name) != std::string::npos; });
        }

        DBusMessage* callMethod(DBusConnection* bus, const char* destination, const char* path, const char* interface, const char* method, int timeoutMs = 500) {
            DBusMessage* message = dbus_message_new_method_call(destination, path, interface, method);
            if (message == nullptr) {
                return nullptr;
            }
            DBusError error;
            dbus_error_init(&error);
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, message, timeoutMs, &error);
            dbus_message_unref(message);
            if (dbus_error_is_set(&error) != FALSE) {
                if (reply != nullptr) {
                    dbus_message_unref(reply);
                    reply = nullptr;
                }
                dbus_error_free(&error);
            }
            return reply;
        }

        DBusMessage* callMethodWithString(DBusConnection* bus, const char* destination, const char* path, const char* interface, const char* method, const char* argument,
                                          int timeoutMs = 500) {
            DBusMessage* message = dbus_message_new_method_call(destination, path, interface, method);
            if (message == nullptr || dbus_message_append_args(message, DBUS_TYPE_STRING, &argument, DBUS_TYPE_INVALID) == FALSE) {
                if (message != nullptr) {
                    dbus_message_unref(message);
                }
                return nullptr;
            }
            DBusError error;
            dbus_error_init(&error);
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, message, timeoutMs, &error);
            dbus_message_unref(message);
            if (dbus_error_is_set(&error) != FALSE) {
                if (reply != nullptr) {
                    dbus_message_unref(reply);
                    reply = nullptr;
                }
                dbus_error_free(&error);
            }
            return reply;
        }

        std::string getAtspiBusAddress() {
            if (const char* address = std::getenv("AT_SPI_BUS_ADDRESS"); address != nullptr && !std::string_view(address).empty()) {
                return address;
            }
            if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir != nullptr && !std::string_view(runtimeDir).empty()) {
                const std::string socketPath = std::string(runtimeDir) + "/at-spi/bus_0";
                if (access(socketPath.c_str(), F_OK) == 0) {
                    return "unix:path=" + socketPath;
                }
            }

            DBusError error;
            dbus_error_init(&error);
            DBusConnection* session = dbus_bus_get(DBUS_BUS_SESSION, &error);
            if (session == nullptr || dbus_error_is_set(&error) != FALSE) {
                dbus_error_free(&error);
                return {};
            }
            DBusMessage* reply = callMethod(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", 200);
            std::string  address;
            if (reply != nullptr) {
                const char* value = nullptr;
                dbus_error_init(&error);
                if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID) != FALSE && value != nullptr) {
                    address = value;
                }
                dbus_error_free(&error);
                dbus_message_unref(reply);
            }
            dbus_connection_unref(session);
            return address;
        }

        bool pingRegistry(DBusConnection* bus) {
            DBusMessage* reply = callMethod(bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/registry", "org.freedesktop.DBus.Peer", "Ping", 150);
            if (reply == nullptr) {
                return false;
            }
            dbus_message_unref(reply);
            return true;
        }

        DBusConnection* connectAtspiBus() {
            const std::string address = getAtspiBusAddress();
            if (!address.empty()) {
                DBusError error;
                dbus_error_init(&error);
                DBusConnection* bus = dbus_connection_open(address.c_str(), &error);
                if (bus != nullptr && dbus_error_is_set(&error) == FALSE && dbus_bus_register(bus, &error) != FALSE && dbus_error_is_set(&error) == FALSE && pingRegistry(bus)) {
                    dbus_connection_set_exit_on_disconnect(bus, FALSE);
                    dbus_error_free(&error);
                    return bus;
                }
                if (bus != nullptr) {
                    dbus_connection_unref(bus);
                }
                dbus_error_free(&error);
            }

            DBusError error;
            dbus_error_init(&error);
            DBusConnection* bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
            if (bus != nullptr && dbus_error_is_set(&error) == FALSE && pingRegistry(bus)) {
                dbus_connection_set_exit_on_disconnect(bus, FALSE);
                dbus_error_free(&error);
                return bus;
            }
            dbus_error_free(&error);
            if (bus != nullptr) {
                dbus_connection_unref(bus);
            }
            return nullptr;
        }

        int queryConnectionPid(DBusConnection* bus, const char* sender) {
            DBusMessage* reply = callMethodWithString(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", sender);
            if (reply == nullptr) {
                return -1;
            }
            dbus_uint32_t pid = 0;
            DBusError     error;
            dbus_error_init(&error);
            const bool ok = dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID) != FALSE;
            dbus_error_free(&error);
            dbus_message_unref(reply);
            return ok ? static_cast<int>(pid) : -1;
        }

        std::string programForPid(int pid) {
            if (pid <= 0) {
                return {};
            }
            char path[64]{};
            char name[64]{};
            std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
            FILE* file = std::fopen(path, "r");
            if (file == nullptr) {
                return {};
            }
            const bool read = std::fgets(name, sizeof(name), file) != nullptr;
            std::fclose(file);
            if (!read) {
                return {};
            }
            name[std::strcspn(name, "\r\n")] = '\0';
            return name;
        }

        std::string queryFocusClass(DBusConnection* bus, const char* sender, const char* path) {
            DBusMessage* reply = callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetAttributes");
            if (reply == nullptr || dbus_message_has_signature(reply, "a{ss}") == FALSE) {
                if (reply != nullptr) {
                    dbus_message_unref(reply);
                }
                return {};
            }
            std::string     className;
            DBusMessageIter iter{};
            DBusMessageIter entries{};
            dbus_message_iter_init(reply, &iter);
            dbus_message_iter_recurse(&iter, &entries);
            while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter entry{};
                dbus_message_iter_recurse(&entries, &entry);
                const char* key   = nullptr;
                const char* value = nullptr;
                dbus_message_iter_get_basic(&entry, &key);
                dbus_message_iter_next(&entry);
                dbus_message_iter_get_basic(&entry, &value);
                if (key != nullptr && value != nullptr && std::strcmp(key, "class") == 0) {
                    className = value;
                    break;
                }
                dbus_message_iter_next(&entries);
            }
            dbus_message_unref(reply);
            return className;
        }

        void pokeAccessibleApps(DBusConnection* bus) {
            DBusMessage* reply = callMethod(bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root", "org.a11y.atspi.Accessible", "GetChildren", 2000);
            if (reply == nullptr) {
                return;
            }
            DBusMessageIter iter{};
            if (dbus_message_iter_init(reply, &iter) == FALSE || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
                dbus_message_unref(reply);
                return;
            }
            DBusMessageIter children{};
            dbus_message_iter_recurse(&iter, &children);
            for (int count = 0; count < kMaxAccessibleNodes && dbus_message_iter_get_arg_type(&children) == DBUS_TYPE_STRUCT; ++count) {
                DBusMessageIter child{};
                dbus_message_iter_recurse(&children, &child);
                const char* appBus  = nullptr;
                const char* appPath = nullptr;
                if (dbus_message_iter_get_arg_type(&child) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&child, static_cast<void*>(&appBus));
                    dbus_message_iter_next(&child);
                    if (dbus_message_iter_get_arg_type(&child) == DBUS_TYPE_OBJECT_PATH) {
                        dbus_message_iter_get_basic(&child, static_cast<void*>(&appPath));
                    }
                }
                if (appBus != nullptr && appPath != nullptr && std::string_view(appPath).starts_with('/')) {
                    if (DBusMessage* poke = callMethod(bus, appBus, appPath, "org.a11y.atspi.Accessible", "GetRelationSet", 500); poke != nullptr) {
                        dbus_message_unref(poke);
                    }
                }
                dbus_message_iter_next(&children);
            }
            dbus_message_unref(reply);
        }

        bool registerEvent(DBusConnection* bus, const char* eventName) {
            DBusMessage* reply = callMethodWithString(bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/registry", "org.a11y.atspi.Registry", "RegisterEvent", eventName, 2000);
            if (reply == nullptr) {
                return false;
            }
            dbus_message_unref(reply);
            return true;
        }

        bool addMatch(DBusConnection* bus, const char* rule) {
            DBusError error;
            dbus_error_init(&error);
            dbus_bus_add_match(bus, rule, &error);
            const bool ok = dbus_error_is_set(&error) == FALSE;
            dbus_error_free(&error);
            return ok;
        }

        bool registerFocusEvents(DBusConnection* bus) {
            return registerEvent(bus, "object:state-changed:focused") && registerEvent(bus, "focus:") &&
                addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Object',member='StateChanged'") &&
                addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Focus'");
        }

        bool isFocusEvent(DBusMessage* message, bool& focused) {
            const char* interface = dbus_message_get_interface(message);
            const char* member    = dbus_message_get_member(message);
            if (interface == nullptr || member == nullptr) {
                return false;
            }
            if (std::string_view(interface) == "org.a11y.atspi.Event.Focus") {
                focused = true;
                return true;
            }
            if (std::string_view(interface) != "org.a11y.atspi.Event.Object" || std::string_view(member) != "StateChanged") {
                return false;
            }
            DBusMessageIter iter{};
            if (dbus_message_iter_init(message, &iter) == FALSE || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
                return false;
            }
            const char* state = nullptr;
            dbus_message_iter_get_basic(&iter, static_cast<void*>(&state));
            if (state == nullptr || std::string_view(state) != "focused") {
                return false;
            }
            dbus_message_iter_next(&iter);
            if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
                return false;
            }
            dbus_int32_t focusState = 0;
            dbus_message_iter_get_basic(&iter, &focusState);
            focused = focusState == 1;
            return true;
        }
    }

    WindowFocusTracker::~WindowFocusTracker() {
        stop();
    }

    bool WindowFocusTracker::start() {
        if (thread_.joinable()) {
            return isValid();
        }
        stopRequested_.store(false, std::memory_order_release);
        thread_ = std::thread(&WindowFocusTracker::threadFunc, this);
        return true;
    }

    void WindowFocusTracker::stop() {
        stopRequested_.store(true, std::memory_order_release);
        valid_.store(false, std::memory_order_relaxed);
        browserUIFocused_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void WindowFocusTracker::threadFunc() {
        while (!stopRequested_.load(std::memory_order_acquire)) {
            DBusConnection* bus = connectAtspiBus();
            if (bus == nullptr || !registerFocusEvents(bus)) {
                if (bus != nullptr) {
                    dbus_connection_unref(bus);
                }
                valid_.store(false, std::memory_order_relaxed);
                browserUIFocused_.store(false, std::memory_order_relaxed);
                for (int retry = 0; retry < 20 && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }
            valid_.store(true, std::memory_order_relaxed);
            pokeAccessibleApps(bus);
            auto nextAccessibilityPoke = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!stopRequested_.load(std::memory_order_acquire) && dbus_connection_read_write(bus, 50) != FALSE) {
                DBusMessage* message = nullptr;
                while ((message = dbus_connection_pop_message(bus)) != nullptr) {
                    bool focused = false;
                    if (isFocusEvent(message, focused)) {
                        const char* sender = dbus_message_get_sender(message);
                        const char* path   = dbus_message_get_path(message);
                        if (!focused) {
                            if (sender != nullptr && focusBus_ == sender && (path == nullptr || focusPath_ == path)) {
                                browserUIFocused_.store(false, std::memory_order_relaxed);
                                focusBus_.clear();
                                focusPath_.clear();
                            }
                        } else if (sender != nullptr && path != nullptr) {
                            focusBus_                   = sender;
                            focusPath_                  = path;
                            const std::string program   = programForPid(queryConnectionPid(bus, sender));
                            const std::string className = isChromiumProgram(program) ? queryFocusClass(bus, sender, path) : std::string{};
                            browserUIFocused_.store(className.find("Omnibox") != std::string::npos, std::memory_order_relaxed);
                        }
                    }
                    dbus_message_unref(message);
                }
                if (std::chrono::steady_clock::now() >= nextAccessibilityPoke) {
                    pokeAccessibleApps(bus);
                    nextAccessibilityPoke = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                }
            }
            valid_.store(false, std::memory_order_relaxed);
            browserUIFocused_.store(false, std::memory_order_relaxed);
            dbus_connection_unref(bus);
            for (int retry = 0; retry < 10 && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}
