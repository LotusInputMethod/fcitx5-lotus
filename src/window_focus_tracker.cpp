/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window_focus_tracker.h"
#include "lotus-dbus.h"
#include "lotus-utils.h"

#include <chrono>
#include <cstring>

namespace fcitx {
    namespace {
        constexpr int                       kMaxAccessibleNodes = 256;  ///< Children walked while priming the tree
        constexpr int                       kPokeTimeoutMs      = 2000; ///< Registry GetChildren / RegisterEvent
        constexpr int                       kReadWriteTimeoutMs = 50;   ///< Signal poll interval
        constexpr int                       kReconnectDelayMs   = 100;  ///< Delay between reconnect attempts
        constexpr int                       kReconnectAttempts  = 20;   ///< Attempts before the bus is retried
        constexpr int                       kRedialAttempts     = 10;   ///< Attempts after a bus loss
        constexpr std::chrono::seconds      kPokeInterval{15};          ///< Accessibility tree priming period
        constexpr int                       kChromiumPokeAttempts = 3;  ///< Fast pokes after focusing a Chromium window
        constexpr std::chrono::milliseconds kChromiumPokeDelay{200};    ///< Spacing of the fast pokes
    } // namespace

    std::string WindowFocusTracker::queryFocusClass(const DbusConnectionPtr& bus, const char* sender, const char* path) {
        DbusMessagePtr reply = dbusCallMethod(bus.get(), sender, path, "org.a11y.atspi.Accessible", "GetAttributes", {}, kPokeTimeoutMs);
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

    void WindowFocusTracker::pokeAccessibleApps(const DbusConnectionPtr& bus) {
        DbusMessagePtr reply =
            dbusCallMethod(bus.get(), "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root", "org.a11y.atspi.Accessible", "GetChildren", {}, kPokeTimeoutMs);
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
                const DbusMessagePtr poke = dbusCallMethod(bus.get(), appBus, appPath, "org.a11y.atspi.Accessible", "GetRelationSet", {}, kPokeTimeoutMs);
                (void)poke;
            }
            dbus_message_iter_next(&children);
        }
    }

    bool WindowFocusTracker::registerEvent(const DbusConnectionPtr& bus, const char* eventName) {
        return dbusCallMethod(bus.get(), "org.a11y.atspi.Registry", "/org/a11y/atspi/registry", "org.a11y.atspi.Registry", "RegisterEvent", eventName, kPokeTimeoutMs) != nullptr;
    }

    bool WindowFocusTracker::addMatch(const DbusConnectionPtr& bus, const char* rule) {
        DbusErrorGuard error;
        dbus_bus_add_match(bus.get(), rule, error.get());
        return !error.isSet();
    }

    bool WindowFocusTracker::registerFocusEvents(const DbusConnectionPtr& bus) {
        return registerEvent(bus, "object:state-changed:focused") && registerEvent(bus, "focus:") &&
            addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Object',member='StateChanged'") && addMatch(bus, "type='signal',interface='org.a11y.atspi.Event.Focus'");
    }

    std::optional<bool> WindowFocusTracker::focusedState(const DbusMessagePtr& message) {
        DBusMessage* const raw       = message.get();
        const char*        interface = dbus_message_get_interface(raw);
        const char*        member    = dbus_message_get_member(raw);
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
        if (dbus_message_iter_init(message.get(), &iter) == FALSE || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
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

    std::optional<std::string> WindowFocusTracker::focusedProgramIfFresh(uint64_t maxAgeMs) const {
        std::scoped_lock lock(programMutex_);
        if (focusedProgram_.empty() || lastFocusEventMs_ == 0 || nowMs() - lastFocusEventMs_ > static_cast<int64_t>(maxAgeMs)) {
            return std::nullopt;
        }
        return focusedProgram_;
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
        {
            std::scoped_lock lock(programMutex_);
            focusedProgram_.clear();
            lastFocusEventMs_ = 0;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void WindowFocusTracker::threadFunc() {
        dbusEnsureThreadsInitialised();
        while (!stopRequested_.load(std::memory_order_acquire)) {
            DbusConnectionPtr bus = dbusConnectAtspiBus();
            if (!bus || !registerFocusEvents(bus)) {
                browserUIFocused_.store(false, std::memory_order_relaxed);
                {
                    std::scoped_lock lock(programMutex_);
                    focusedProgram_.clear();
                    lastFocusEventMs_ = 0;
                }
                for (int retry = 0; retry < kReconnectAttempts && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
                }
                continue;
            }
            std::string focusBus;
            std::string focusPath;
            int         chromiumPokesLeft = 0;
            pokeAccessibleApps(bus);
            auto nextAccessibilityPoke = std::chrono::steady_clock::now() + kPokeInterval;
            while (!stopRequested_.load(std::memory_order_acquire) && dbus_connection_read_write(bus.get(), kReadWriteTimeoutMs) != FALSE) {
                while (DbusMessagePtr message{dbus_connection_pop_message(bus.get())}) {
                    if (const std::optional<bool> focused = focusedState(message)) {
                        const char* sender = dbus_message_get_sender(message.get());
                        const char* path   = dbus_message_get_path(message.get());
                        if (!*focused) {
                            if (sender != nullptr && focusBus == sender && (path == nullptr || focusPath == path)) {
                                browserUIFocused_.store(false, std::memory_order_relaxed);
                                std::scoped_lock lock(programMutex_);
                                focusedProgram_.clear();
                                lastFocusEventMs_ = nowMs();
                                focusBus.clear();
                                focusPath.clear();
                            }
                        } else if (sender != nullptr && path != nullptr) {
                            focusBus                    = sender;
                            focusPath                   = path;
                            const std::string program   = programNameForBusName(sender);
                            const bool        chromium  = isChromiumProgram(program);
                            const std::string className = chromium ? queryFocusClass(bus, sender, path) : std::string{};
                            browserUIFocused_.store(className.find("Omnibox") != std::string::npos, std::memory_order_relaxed);
                            {
                                std::scoped_lock lock(programMutex_);
                                focusedProgram_   = program;
                                lastFocusEventMs_ = nowMs();
                            }
                            if (chromium && !browserUIFocused_.load(std::memory_order_relaxed)) {
                                pokeAccessibleApps(bus);
                                chromiumPokesLeft     = kChromiumPokeAttempts;
                                nextAccessibilityPoke = std::chrono::steady_clock::now() + kChromiumPokeDelay;
                            }
                        }
                    }
                }
                if (std::chrono::steady_clock::now() >= nextAccessibilityPoke) {
                    pokeAccessibleApps(bus);
                    if (chromiumPokesLeft > 0) {
                        --chromiumPokesLeft;
                        nextAccessibilityPoke = std::chrono::steady_clock::now() + kChromiumPokeDelay;
                    } else {
                        nextAccessibilityPoke = std::chrono::steady_clock::now() + kPokeInterval;
                    }
                }
            }
            browserUIFocused_.store(false, std::memory_order_relaxed);
            {
                std::scoped_lock lock(programMutex_);
                focusedProgram_.clear();
                lastFocusEventMs_ = 0;
            }
            for (int retry = 0; retry < kRedialAttempts && !stopRequested_.load(std::memory_order_acquire); ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
            }
        }
    }
} // namespace fcitx
