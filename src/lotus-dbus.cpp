/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-dbus.h"

#include <cstdlib>

#include <unistd.h>

namespace {
    constexpr int         kShortTimeoutMs   = 150; ///< Ping / GetAddress round trip
    constexpr int         kDefaultTimeoutMs = 500; ///< Generic method calls
    constexpr const char* kAtspiBus         = "org.a11y.Bus";
    constexpr const char* kAtspiBusPath     = "/org/a11y/bus";
    constexpr const char* kRegistry         = "org.a11y.atspi.Registry";
    constexpr const char* kRegistryPath     = "/org/a11y/atspi/registry";

    /**
     * @brief Checks that the AT-SPI registry answers on the given bus.
     * @param bus Connection to ping.
     * @return True when the registry replied.
     */
    bool pingRegistry(DBusConnection* bus) {
        return dbusCallMethod(bus, kRegistry, kRegistryPath, "org.freedesktop.DBus.Peer", "Ping", {}, kShortTimeoutMs) != nullptr;
    }
} // namespace

DbusErrorGuard::DbusErrorGuard() {
    dbus_error_init(&error_);
}

DbusErrorGuard::~DbusErrorGuard() {
    dbus_error_free(&error_);
}

DBusError* DbusErrorGuard::get() {
    return &error_;
}

bool DbusErrorGuard::isSet() const {
    return dbus_error_is_set(&error_) != FALSE;
}

void DbusErrorGuard::clear() {
    dbus_error_free(&error_);
}

void DbusConnectionDeleter::operator()(DBusConnection* connection) const noexcept {
    if (connection == nullptr) {
        return;
    }
    if (isPrivate) {
        dbus_connection_close(connection);
    }
    dbus_connection_unref(connection);
}

void DbusMessageDeleter::operator()(DBusMessage* message) const noexcept {
    if (message != nullptr) {
        dbus_message_unref(message);
    }
}

void dbusEnsureThreadsInitialised() {
    static const bool initialised = dbus_threads_init_default() != FALSE;
    (void)initialised;
}

DbusMessagePtr dbusCallMethod(DBusConnection* connection, const char* destination, const char* path, const char* interface, const char* method, std::string_view argument,
                              int timeoutMs) {
    DbusMessagePtr request(dbus_message_new_method_call(destination, path, interface, method));
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
    DbusErrorGuard error;
    DbusMessagePtr reply(dbus_connection_send_with_reply_and_block(connection, request.get(), timeoutMs, error.get()));
    if (error.isSet()) {
        return nullptr;
    }
    return reply;
}

std::string dbusAtspiBusAddress() {
    if (const char* address = std::getenv("AT_SPI_BUS_ADDRESS"); address != nullptr && !std::string_view(address).empty()) {
        return address;
    }
    if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir != nullptr && !std::string_view(runtimeDir).empty()) {
        const std::string socketPath = std::string(runtimeDir) + "/at-spi/bus_0";
        if (access(socketPath.c_str(), F_OK) == 0) {
            return "unix:path=" + socketPath;
        }
    }

    DbusErrorGuard    error;
    DbusConnectionPtr session(dbus_bus_get(DBUS_BUS_SESSION, error.get()), DbusConnectionDeleter{});
    if (!session || error.isSet()) {
        return {};
    }

    DbusMessagePtr reply = dbusCallMethod(session.get(), kAtspiBus, kAtspiBusPath, kAtspiBus, "GetAddress", {}, kShortTimeoutMs);
    std::string    address;
    if (reply) {
        char*          value = nullptr;
        DbusErrorGuard argsError;
        if (dbus_message_get_args(reply.get(), argsError.get(), DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID) != FALSE && value != nullptr) {
            address = value;
        }
    }
    return address;
}

DbusConnectionPtr dbusConnectAtspiBus() {
    const std::string address = dbusAtspiBusAddress();
    if (!address.empty()) {
        DbusErrorGuard    error;
        DbusConnectionPtr bus(dbus_connection_open(address.c_str(), error.get()), DbusConnectionDeleter{});
        if (bus && !error.isSet() && dbus_bus_register(bus.get(), error.get()) != FALSE && !error.isSet() && pingRegistry(bus.get())) {
            dbus_connection_set_exit_on_disconnect(bus.get(), FALSE);
            return bus;
        }
    }

    DbusErrorGuard    error;
    DbusConnectionPtr bus(dbus_bus_get_private(DBUS_BUS_SESSION, error.get()), DbusConnectionDeleter{.isPrivate = true});
    if (bus && !error.isSet() && pingRegistry(bus.get())) {
        dbus_connection_set_exit_on_disconnect(bus.get(), FALSE);
        return bus;
    }
    return nullptr;
}

int dbusConnectionPid(DBusConnection* connection, const char* busName) {
    if (connection == nullptr || busName == nullptr) {
        return -1;
    }
    DbusMessagePtr reply =
        dbusCallMethod(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", busName, kDefaultTimeoutMs);
    if (!reply) {
        return -1;
    }
    dbus_uint32_t  pid = 0;
    DbusErrorGuard error;
    const bool     ok = dbus_message_get_args(reply.get(), error.get(), DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID) != FALSE;
    return ok ? static_cast<int>(pid) : -1;
}
