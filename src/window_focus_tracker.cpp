/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window_focus_tracker.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

#include <dbus/dbus.h>
#include <unistd.h>

namespace fcitx {
    namespace {
        constexpr int kDocumentWebRole    = 95;
        constexpr int kDocumentFrameRole  = 82;
        constexpr int kMaxAncestorDepth   = 64;
        constexpr int kMaxAccessibleNodes = 256;

        DBusMessage*  callMethod(DBusConnection* bus, const char* destination, const char* path, const char* interface, const char* method, int timeoutMs = 500) {
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

        int queryRole(DBusConnection* bus, const char* sender, const char* path) {
            DBusMessage* reply = callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetRole");
            if (reply == nullptr) {
                return -1;
            }
            dbus_uint32_t role = 0;
            DBusError     error;
            dbus_error_init(&error);
            const bool ok = dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &role, DBUS_TYPE_INVALID) != FALSE;
            dbus_error_free(&error);
            dbus_message_unref(reply);
            return ok ? static_cast<int>(role) : -1;
        }

        bool queryParent(DBusConnection* bus, const char* sender, const char* path, std::string& parentSender, std::string& parentPath) {
            constexpr const char* accessibleInterface = "org.a11y.atspi.Accessible";
            DBusMessage*          message             = dbus_message_new_method_call(sender, path, "org.freedesktop.DBus.Properties", "Get");
            constexpr const char* property            = "Parent";
            if (message == nullptr || dbus_message_append_args(message, DBUS_TYPE_STRING, &accessibleInterface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID) == FALSE) {
                if (message != nullptr) {
                    dbus_message_unref(message);
                }
                return false;
            }
            DBusError error;
            dbus_error_init(&error);
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, message, 500, &error);
            dbus_message_unref(message);
            dbus_error_free(&error);
            if (reply == nullptr) {
                return false;
            }

            DBusMessageIter iter{};
            DBusMessageIter variant{};
            DBusMessageIter structure{};
            bool            ok = false;
            if (dbus_message_iter_init(reply, &iter) != FALSE && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
                dbus_message_iter_recurse(&iter, &variant);
                if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRUCT) {
                    dbus_message_iter_recurse(&variant, &structure);
                    const char* busName    = nullptr;
                    const char* objectPath = nullptr;
                    if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&structure, static_cast<void*>(&busName));
                        dbus_message_iter_next(&structure);
                        if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_OBJECT_PATH) {
                            dbus_message_iter_get_basic(&structure, static_cast<void*>(&objectPath));
                        }
                    }
                    if (busName != nullptr && objectPath != nullptr && std::string_view(objectPath).starts_with('/')) {
                        parentSender = busName;
                        parentPath   = objectPath;
                        ok           = true;
                    }
                }
            }
            dbus_message_unref(reply);
            return ok;
        }

        bool hasDocumentAncestor(DBusConnection* bus, const char* sender, const char* path) {
            std::string currentSender = sender;
            std::string currentPath   = path;
            for (int depth = 0; depth < kMaxAncestorDepth; ++depth) {
                std::string parentSender;
                std::string parentPath;
                if (!queryParent(bus, currentSender.c_str(), currentPath.c_str(), parentSender, parentPath) || parentPath == "/org/a11y/atspi/null" ||
                    parentPath == "/org/a11y/atspi/accessible/root") {
                    break;
                }
                const int role = queryRole(bus, parentSender.c_str(), parentPath.c_str());
                if (role == kDocumentWebRole || role == kDocumentFrameRole) {
                    return true;
                }
                currentSender = std::move(parentSender);
                currentPath   = std::move(parentPath);
            }
            return false;
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
                            focusBus_  = sender;
                            focusPath_ = path;
                            browserUIFocused_.store(!hasDocumentAncestor(bus, sender, path), std::memory_order_relaxed);
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