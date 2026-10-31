/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "window_focus_tracker.h"
#include "lotus-utils.h"

#include <array>
#include <chrono>
#include <cstring>

#include <unistd.h>

namespace fcitx {
    namespace {
        constexpr int                  kMaxAccessibleNodes = 256;  ///< Children walked while priming the tree
        constexpr int                  kShortTimeoutMs     = 150;  ///< Ping / GetAddress round trip
        constexpr int                  kDefaultTimeoutMs   = 500;  ///< GetAttributes / GetRelationSet
        constexpr int                  kPokeTimeoutMs      = 2000; ///< Registry GetChildren
        constexpr int                  kReadWriteTimeoutMs = 50;   ///< Signal poll interval
        constexpr int                  kReconnectDelayMs   = 100;  ///< Delay between reconnect attempts
        constexpr int                  kReconnectAttempts  = 20;   ///< Attempts before the bus is retried
        constexpr int                  kRedialAttempts     = 10;   ///< Attempts after a bus loss
        constexpr std::chrono::seconds kPokeInterval{15};          ///< Accessibility tree priming period
        constexpr std::size_t          kProcPathSize = 64;         ///< "/proc/<pid>/comm" buffer size
    } // namespace

    void WindowFocusTracker::ConnectionDeleter::operator()(DBusConnection* connection) const noexcept {
        if (connection == nullptr) {
            return;
        }
        if (isPrivate) {
            dbus_connection_close(connection);
        }
        dbus_connection_unref(connection);
    }

    void WindowFocusTracker::MessageDeleter::operator()(DBusMessage* message) const noexcept {
        if (message != nullptr) {
            dbus_message_unref(message);
        }
    }

    void WindowFocusTracker::ensureDbusThreadsInitialised() {
        static const bool initialised = dbus_threads_init_default() != FALSE;
        (void)initialised;
    }

    WindowFocusTracker::MessagePtr WindowFocusTracker::callMethod(DBusConnection* bus, const char* destination, const char* path, const char* interface, const char* method,
                                                                  std::string_view argument, int timeoutMs) {
        MessagePtr request(dbus_message_new_method_call(destination, path, interface, method));
        if (!request) {
            return nullptr;
        }
        if (!argument.empty()) {
            const std::string value{argument};
            const char*       raw = value.c_str();
            if (dbus_message_append_args(request.get(), DBUS_TYPE_STRING, &raw, DBUS_TYPE_INVALID) == FALSE) {
                return nullptr;
            }
        }
        DBusError error;
        dbus_error_init(&error);
        MessagePtr reply(dbus_connection_send_with_reply_and_block(bus, request.get(), timeoutMs, &error));
        if (dbus_error_is_set(&error) != FALSE) {
            dbus_error_free(&error);
            return nullptr;
        }
        dbus_error_free(&error);
        return reply;
    }

    std::string WindowFocusTracker::getAtspiBusAddress() {
        std::string envAddress = getEnv("AT_SPI_BUS_ADDRESS");
        if (!envAddress.empty()) {
            return envAddress;
        }
        const std::string runtimeDir = getEnv("XDG_RUNTIME_DIR");
        if (!runtimeDir.empty()) {
            const std::string socketPath = runtimeDir + "/at-spi/bus_0";
            if (access(socketPath.c_str(), F_OK) == 0) {
                return "unix:path=" + socketPath;
            }
        }

        DBusError error;
        dbus_error_init(&error);
        ConnectionPtr session(dbus_bus_get(DBUS_BUS_SESSION, &error), ConnectionDeleter{});
        if (!session || dbus_error_is_set(&error) != FALSE) {
            dbus_error_free(&error);
            return {};
        }
        dbus_error_free(&error);

        MessagePtr  reply = callMethod(session.get(), "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", {}, kShortTimeoutMs);
        std::string address;
        if (reply) {
            char*     value = nullptr;
            DBusError argsError;
            dbus_error_init(&argsError);
            if (dbus_message_get_args(reply.get(), &argsError, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID) != FALSE && value != nullptr) {
                address = value;
            }
            dbus_error_free(&argsError);
        }
        return address;
    }

    bool WindowFocusTracker::pingRegistry(DBusConnection* bus) {
        return callMethod(bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/registry", "org.freedesktop.DBus.Peer", "Ping", {}, kShortTimeoutMs) != nullptr;
    }

    WindowFocusTracker::ConnectionPtr WindowFocusTracker::connectAtspiBus() {
        const std::string address = getAtspiBusAddress();
        if (!address.empty()) {
            DBusError error;
            dbus_error_init(&error);
            ConnectionPtr bus(dbus_connection_open(address.c_str(), &error), ConnectionDeleter{});
            if (bus && dbus_error_is_set(&error) == FALSE && dbus_bus_register(bus.get(), &error) != FALSE && dbus_error_is_set(&error) == FALSE && pingRegistry(bus.get())) {
                dbus_error_free(&error);
                dbus_connection_set_exit_on_disconnect(bus.get(), FALSE);
                return bus;
            }
            dbus_error_free(&error);
        }

        DBusError error;
        dbus_error_init(&error);
        ConnectionPtr bus(dbus_bus_get_private(DBUS_BUS_SESSION, &error), ConnectionDeleter{.isPrivate = true});
        if (bus && dbus_error_is_set(&error) == FALSE && pingRegistry(bus.get())) {
            dbus_error_free(&error);
            dbus_connection_set_exit_on_disconnect(bus.get(), FALSE);
            return bus;
        }
        dbus_error_free(&error);
        return nullptr;
    }

    int WindowFocusTracker::queryConnectionPid(DBusConnection* bus, const char* sender) {
        MessagePtr reply = callMethod(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", sender, kDefaultTimeoutMs);
        if (!reply) {
            return -1;
        }
        dbus_uint32_t pid = 0;
        DBusError     error;
        dbus_error_init(&error);
        const bool ok = dbus_message_get_args(reply.get(), &error, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID) != FALSE;
        dbus_error_free(&error);
        return ok ? static_cast<int>(pid) : -1;
    }

    std::string WindowFocusTracker::programForPid(int pid) {
        if (pid <= 0) {
            return {};
        }
        std::array<char, kProcPathSize> path{};
        std::array<char, kProcPathSize> name{};
        std::snprintf(path.data(), path.size(), "/proc/%d/comm", pid);
        FilePtr file(std::fopen(path.data(), "r"), &std::fclose);
        if (!file) {
            return {};
        }
        if (std::fgets(name.data(), static_cast<int>(name.size()), file.get()) == nullptr) {
            return {};
        }
        name.at(std::strcspn(name.data(), "\r\n")) = '\0';
        return name.data();
    }

    std::string WindowFocusTracker::queryFocusClass(DBusConnection* bus, const char* sender, const char* path) {
        MessagePtr reply = callMethod(bus, sender, path, "org.a11y.atspi.Accessible", "GetAttributes", {}, kDefaultTimeoutMs);
        if (!reply || dbus_message_has_signature(reply.get(), "a{ss}") == FALSE) {
            return {};
        }
        DBusMessageIter iter{};
        if (dbus_message_iter_init(reply.get(), &iter) == FALSE) {
            return {};
        }
        DBusMessageIter entries{};
        dbus_message_iter_recurse(&iter, &entries);
        while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry{};
            dbus_message_iter_recurse(&entries, &entry);
            char* key   = nullptr;
            char* value = nullptr;
            dbus_message_iter_get_basic(&entry, reinterpret_cast<void*>(&key));
            dbus_message_iter_next(&entry);
            dbus_message_iter_get_basic(&entry, reinterpret_cast<void*>(&value));
            if (key != nullptr && value != nullptr && std::strcmp(key, "class") == 0) {
                return value;
            }
            dbus_message_iter_next(&entries);
        }
        return {};
    }

    void WindowFocusTracker::pokeAccessibleApps(DBusConnection* bus) {
        MessagePtr reply = callMethod(bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root", "org.a11y.atspi.Accessible", "GetChildren", {}, kPokeTimeoutMs);
        if (!reply) {
            return;
        }
        DBusMessageIter iter{};
        if (dbus_message_iter_init(reply.get(), &iter) == FALSE || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
            return;
        }
        DBusMessageIter children{};
        dbus_message_iter_recurse(&iter, &children);
        for (int count = 0; count < kMaxAccessibleNodes && dbus_message_iter_get_arg_type(&children) == DBUS_TYPE_STRUCT; ++count) {
            DBusMessageIter child{};
            dbus_message_iter_recurse(&children, &child);
            char* appBus  = nullptr;
            char* appPath = nullptr;
            if (dbus_message_iter_get_arg_type(&child) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&child, reinterpret_cast<void*>(&appBus));
                dbus_message_iter_next(&child);
                if (dbus_message_iter_get_arg_type(&child) == DBUS_TYPE_OBJECT_PATH) {
                    dbus_message_iter_get_basic(&child, reinterpret_cast<void*>(&appPath));
                }
            }
            if (appBus != nullptr && appPath != nullptr && std::string_view(appPath).starts_with('/')) {
                const MessagePtr poke = callMethod(bus, appBus, appPath, "org.a11y.atspi.Accessible", "GetRelationSet", {}, kDefaultTimeoutMs);
                (void)poke;
            }
            dbus_message_iter_next(&children);
        }
    }

    bool WindowFocusTracker::registerEvent(DBusConnection* bus, const char* eventName) {
        return callMethod(bus, "org.a11y.atspi.Registry", "/org/a11y/atspi/registry", "org.a11y.atspi.Registry", "RegisterEvent", eventName, kPokeTimeoutMs) != nullptr;
    }

    bool WindowFocusTracker::addMatch(DBusConnection* bus, const char* rule) {
        DBusError error;
        dbus_error_init(&error);
        dbus_bus_add_match(bus, rule, &error);
        const bool ok = dbus_error_is_set(&error) == FALSE;
        dbus_error_free(&error);
        return ok;
    }

    bool WindowFocusTracker::registerFocusEvents(DBusConnection* bus) {
        return registerEvent(bus, "object:state-changed:focused") && registerEvent(bus, "focus:") &&
            addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Object',member='StateChanged'") && addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Focus'");
    }

    std::optional<bool> WindowFocusTracker::focusedState(DBusMessage* message) {
        const char* interface = dbus_message_get_interface(message);
        const char* member    = dbus_message_get_member(message);
        if (interface == nullptr || member == nullptr) {
            return std::nullopt;
        }
        if (std::string_view(interface) == "org.a11y.atspi.Event.Focus") {
            return true;
        }
        if (std::string_view(interface) != "org.a11y.atspi.Event.Object" || std::string_view(member) != "StateChanged") {
            return std::nullopt;
        }
        DBusMessageIter iter{};
        if (dbus_message_iter_init(message, &iter) == FALSE || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
            return std::nullopt;
        }
        char* state = nullptr;
        dbus_message_iter_get_basic(&iter, reinterpret_cast<void*>(&state));
        if (state == nullptr || std::string_view(state) != "focused") {
            return std::nullopt;
        }
        dbus_message_iter_next(&iter);
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
            return std::nullopt;
        }
        dbus_int32_t focusState = 0;
        dbus_message_iter_get_basic(&iter, &focusState);
        return focusState == 1;
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
        browserUIFocused_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void WindowFocusTracker::threadFunc() {
        ensureDbusThreadsInitialised();
        while (!stopRequested_.load(std::memory_order_acquire)) {
            ConnectionPtr bus = connectAtspiBus();
            if (!bus || !registerFocusEvents(bus.get())) {
                browserUIFocused_.store(false, std::memory_order_relaxed);
                for (int retry = 0; retry < kReconnectAttempts && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
                }
                continue;
            }
            std::string focusBus;
            std::string focusPath;
            pokeAccessibleApps(bus.get());
            auto nextAccessibilityPoke = std::chrono::steady_clock::now() + kPokeInterval;
            while (!stopRequested_.load(std::memory_order_acquire) && dbus_connection_read_write(bus.get(), kReadWriteTimeoutMs) != FALSE) {
                while (MessagePtr message{dbus_connection_pop_message(bus.get())}) {
                    if (const std::optional<bool> focused = focusedState(message.get())) {
                        const char* sender = dbus_message_get_sender(message.get());
                        const char* path   = dbus_message_get_path(message.get());
                        if (!*focused) {
                            if (sender != nullptr && focusBus == sender && (path == nullptr || focusPath == path)) {
                                browserUIFocused_.store(false, std::memory_order_relaxed);
                                focusBus.clear();
                                focusPath.clear();
                            }
                        } else if (sender != nullptr && path != nullptr) {
                            focusBus                    = sender;
                            focusPath                   = path;
                            const std::string program   = programForPid(queryConnectionPid(bus.get(), sender));
                            const std::string className = isChromiumProgram(program) ? queryFocusClass(bus.get(), sender, path) : std::string{};
                            browserUIFocused_.store(className.find("Omnibox") != std::string::npos, std::memory_order_relaxed);
                        }
                    }
                }
                if (std::chrono::steady_clock::now() >= nextAccessibilityPoke) {
                    pokeAccessibleApps(bus.get());
                    nextAccessibilityPoke = std::chrono::steady_clock::now() + kPokeInterval;
                }
            }
            browserUIFocused_.store(false, std::memory_order_relaxed);
            for (int retry = 0; retry < kRedialAttempts && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
            }
        }
    }
} // namespace fcitx
