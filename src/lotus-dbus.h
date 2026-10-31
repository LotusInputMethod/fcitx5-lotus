/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file lotus-dbus.h
 * @brief Thin libdbus helpers shared by the addon (AT-SPI focus tracking and process lookups).
 */

#ifndef _LOTUS_DBUS_H_
#define _LOTUS_DBUS_H_

#include <memory>
#include <string>
#include <string_view>

#include <dbus/dbus.h>

/**
 * @brief RAII guard for a DBusError: initialised on construction, freed when the guard dies.
 */
class DbusErrorGuard final {
  public:
    DbusErrorGuard();
    ~DbusErrorGuard();

    DbusErrorGuard(const DbusErrorGuard&)            = delete;
    DbusErrorGuard& operator=(const DbusErrorGuard&) = delete;
    DbusErrorGuard(DbusErrorGuard&&)                 = delete;
    DbusErrorGuard& operator=(DbusErrorGuard&&)      = delete;

    /**
     * @brief Error handed to libdbus calls.
     * @return Wrapped error.
     */
    [[nodiscard]] DBusError* get();

    /**
     * @brief Reports whether libdbus set the error.
     * @return True when the error is set.
     */
    [[nodiscard]] bool isSet() const;

    /**
     * @brief Clears the error so the same guard can be reused by the next call.
     */
    void clear();

  private:
    DBusError error_{};
};

/**
 * @brief Releases a libdbus connection; a private connection is closed before the unref.
 */
struct DbusConnectionDeleter {
    bool isPrivate = false; ///< True for connections opened with dbus_bus_get_private

    /**
     * @brief Closes and unrefs the connection.
     * @param connection Connection to release, may be null.
     */
    void operator()(DBusConnection* connection) const noexcept;
};

/**
 * @brief Releases a libdbus message.
 */
struct DbusMessageDeleter {
    /**
     * @brief Unrefs the message.
     * @param message Message to release, may be null.
     */
    void operator()(DBusMessage* message) const noexcept;
};

using DbusConnectionPtr = std::unique_ptr<DBusConnection, DbusConnectionDeleter>; ///< Owning bus connection
using DbusMessagePtr    = std::unique_ptr<DBusMessage, DbusMessageDeleter>;       ///< Owning D-Bus message

/**
 * @brief Initialises libdbus threading once, required because fcitx also uses dbus.
 */
void dbusEnsureThreadsInitialised();

/**
 * @brief Calls a D-Bus method and waits for its reply.
 * @param connection Connection to use.
 * @param destination Bus name owning the object.
 * @param path Object path.
 * @param interface Interface name.
 * @param method Method name.
 * @param argument String argument; empty sends no argument.
 * @param timeoutMs Reply timeout in milliseconds.
 * @return Reply message, or null when the call failed.
 */
DbusMessagePtr dbusCallMethod(DBusConnection* connection, const char* destination, const char* path, const char* interface, const char* method, std::string_view argument,
                              int timeoutMs);

/**
 * @brief Reads the AT-SPI bus address from the environment, the runtime dir or org.a11y.Bus.GetAddress.
 * @return Bus address, empty when it cannot be determined.
 */
std::string dbusAtspiBusAddress();

/**
 * @brief Opens and registers a connection to the AT-SPI bus, falling back to a private session connection.
 * @return Connection holding the bus, or null when neither answers.
 */
DbusConnectionPtr dbusConnectAtspiBus();

/**
 * @brief Resolves the process owning a bus name.
 * @param connection Connection to ask.
 * @param busName Unique or well-known bus name.
 * @return Process id, or -1 when it cannot be resolved.
 */
int dbusConnectionPid(DBusConnection* connection, const char* busName);

#endif // _LOTUS_DBUS_H_
