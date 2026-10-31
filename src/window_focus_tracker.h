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

#include "lotus-dbus.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace fcitx {

    /**
     * @brief Watches AT-SPI focus events and reports the focused application.
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

        /**
         * @brief Reports the program behind the focused accessibility window when that focus is recent.
         * @param maxAgeMs Maximum age of the last AT-SPI focus event.
         * @return Program name, or nullopt when no accessible window holds the focus or the event is too old.
         */
        [[nodiscard]] std::optional<std::string> focusedProgramIfFresh(uint64_t maxAgeMs) const;

      private:
        /**
         * @brief Reads the "class" attribute of an accessibility node.
         * @param bus Connection to use.
         * @param sender Bus name owning the node.
         * @param path Object path of the node.
         * @return Class attribute, empty when the node has none.
         */
        static std::string queryFocusClass(const DbusConnectionPtr& bus, const char* sender, const char* path);

        /**
         * @brief Walks the accessibility tree once so Chromium exposes its address bar node.
         * @param bus Connection to use.
         */
        static void pokeAccessibleApps(const DbusConnectionPtr& bus);

        /**
         * @brief Registers one AT-SPI event name.
         * @param bus Connection to use.
         * @param eventName Event to register.
         * @return True when the registry accepted the request.
         */
        static bool registerEvent(const DbusConnectionPtr& bus, const char* eventName);

        /**
         * @brief Adds one signal match rule.
         * @param bus Connection to use.
         * @param rule Match rule.
         * @return True when the rule was accepted.
         */
        static bool addMatch(const DbusConnectionPtr& bus, const char* rule);

        /**
         * @brief Subscribes to the AT-SPI focus events and their match rules.
         * @param bus Connection to use.
         * @return True when every request succeeded.
         */
        static bool registerFocusEvents(const DbusConnectionPtr& bus);

        /**
         * @brief Classifies an incoming signal as a focus event.
         * @param message Signal to inspect.
         * @return Focus state the signal reports; nullopt when it is not a focus event.
         */
        static std::optional<bool> focusedState(const DbusMessagePtr& message);

        /**
         * @brief Body of the tracker thread: connects to the AT-SPI bus and follows focus events.
         */
        void               threadFunc();

        std::thread        thread_;                  ///< Thread following the accessibility focus
        std::atomic<bool>  stopRequested_{false};    ///< Set by stop() to end the thread
        std::atomic<bool>  browserUIFocused_{false}; ///< True while a Chromium Omnibox holds the focus
        mutable std::mutex programMutex_;            ///< Guards focusedProgram_ and lastFocusEventMs_
        std::string        focusedProgram_;          ///< Program behind the focused accessibility window
        int64_t            lastFocusEventMs_ = 0;    ///< Monotonic ms of the last AT-SPI focus event, 0 when unknown
    };

} // namespace fcitx

#endif // _SRC_WINDOW_FOCUS_TRACKER_H_
