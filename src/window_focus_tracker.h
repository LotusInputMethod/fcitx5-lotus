/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file window_focus_tracker.h
 * @brief Tracks the AT-SPI focus of Chromium based browsers to recognise their address bar.
 */

#ifndef _SRC_WINDOW_FOCUS_TRACKER_H_
#define _SRC_WINDOW_FOCUS_TRACKER_H_

#include <atomic>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <dbus/dbus.h>

namespace fcitx {

    /**
     * @brief Watches AT-SPI focus events and reports whether a Chromium address bar is focused.
     */
    class WindowFocusTracker {
      public:
        WindowFocusTracker() = default;

        /**
         * @brief Stops the tracker thread.
         */
        ~WindowFocusTracker();

        WindowFocusTracker(const WindowFocusTracker&)            = delete;
        WindowFocusTracker& operator=(const WindowFocusTracker&) = delete;
        WindowFocusTracker(WindowFocusTracker&&)                 = delete;
        WindowFocusTracker& operator=(WindowFocusTracker&&)      = delete;

        /**
         * @brief Starts the tracker thread; does nothing when it already runs.
         */
        void start();

        /**
         * @brief Stops the tracker thread and clears the reported focus state.
         */
        void stop();

        /**
         * @brief Reports whether the focused accessibility node is a Chromium address bar.
         * @return True while the Omnibox of a Chromium browser holds the accessibility focus.
         */
        [[nodiscard]] bool isLikeAddressBarChromium() const {
            return browserUIFocused_.load(std::memory_order_relaxed);
        }

      private:
        /**
         * @brief Releases a libdbus connection; private connections must be closed before unref.
         */
        struct ConnectionDeleter {
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
        struct MessageDeleter {
            /**
             * @brief Unrefs the message.
             * @param message Message to release, may be null.
             */
            void operator()(DBusMessage* message) const noexcept;
        };

        using ConnectionPtr = std::unique_ptr<DBusConnection, ConnectionDeleter>; ///< Owning bus connection
        using MessagePtr    = std::unique_ptr<DBusMessage, MessageDeleter>;       ///< Owning D-Bus message
        using FilePtr       = std::unique_ptr<FILE, decltype(&std::fclose)>;      ///< Owning stdio file

        /**
         * @brief Initialises libdbus threading once, required because fcitx also uses dbus.
         */
        static void ensureDbusThreadsInitialised();

        /**
         * @brief Calls a D-Bus method and waits for its reply.
         * @param bus Connection to use.
         * @param destination Bus name owning the object.
         * @param path Object path.
         * @param interface Interface name.
         * @param method Method name.
         * @param argument String argument; empty sends no argument.
         * @param timeoutMs Reply timeout.
         * @return Reply message, or null when the call failed.
         */
        static MessagePtr callMethod(DBusConnection* bus, const char* destination, const char* path, const char* interface, const char* method, std::string_view argument,
                                     int timeoutMs);

        /**
         * @brief Reads the AT-SPI bus address from the environment, the runtime dir or the a11y service.
         * @return Bus address, empty when it cannot be determined.
         */
        static std::string getAtspiBusAddress();

        /**
         * @brief Checks that the AT-SPI registry answers on the given bus.
         * @param bus Connection to ping.
         * @return True when the registry replied.
         */
        static bool pingRegistry(DBusConnection* bus);

        /**
         * @brief Opens the AT-SPI bus, falling back to a private session bus connection.
         * @return Connection holding the bus, or null when neither answers.
         */
        static ConnectionPtr connectAtspiBus();

        /**
         * @brief Resolves the process id owning a bus name.
         * @param bus Connection to use.
         * @param sender Bus name of the sender.
         * @return Process id, or -1 when it cannot be resolved.
         */
        static int queryConnectionPid(DBusConnection* bus, const char* sender);

        /**
         * @brief Reads the executable name of a process from /proc.
         * @param pid Process id.
         * @return Process name, empty when it cannot be read.
         */
        static std::string programForPid(int pid);

        /**
         * @brief Reads the "class" attribute of an accessibility node.
         * @param bus Connection to use.
         * @param sender Bus name owning the node.
         * @param path Object path of the node.
         * @return Class attribute, empty when the node has none.
         */
        static std::string queryFocusClass(DBusConnection* bus, const char* sender, const char* path);

        /**
         * @brief Walks the accessibility tree once so Chromium exposes its address bar node.
         * @param bus Connection to use.
         */
        static void pokeAccessibleApps(DBusConnection* bus);

        /**
         * @brief Registers one AT-SPI event name.
         * @param bus Connection to use.
         * @param eventName Event to register.
         * @return True when the registry accepted the request.
         */
        static bool registerEvent(DBusConnection* bus, const char* eventName);

        /**
         * @brief Adds one signal match rule.
         * @param bus Connection to use.
         * @param rule Match rule.
         * @return True when the rule was accepted.
         */
        static bool addMatch(DBusConnection* bus, const char* rule);

        /**
         * @brief Subscribes to the AT-SPI focus events and their match rules.
         * @param bus Connection to use.
         * @return True when every request succeeded.
         */
        static bool registerFocusEvents(DBusConnection* bus);

        /**
         * @brief Classifies an incoming signal as a focus event.
         * @param message Signal to inspect.
         * @return Focus state the signal reports; nullopt when it is not a focus event.
         */
        static std::optional<bool> focusedState(DBusMessage* message);

        /**
         * @brief Body of the tracker thread: connects to the AT-SPI bus and follows focus events.
         */
        void              threadFunc();

        std::thread       thread_;                  ///< Thread following the accessibility focus
        std::atomic<bool> stopRequested_{false};    ///< Set by stop() to end the thread
        std::atomic<bool> browserUIFocused_{false}; ///< True while a Chromium Omnibox holds the focus
    };

} // namespace fcitx

#endif // _SRC_WINDOW_FOCUS_TRACKER_H_
