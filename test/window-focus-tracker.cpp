// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file window-focus-tracker.cpp
 * @brief Regressions for idle accessibility focus and late app rules on GNOME's shared context.
 */

#include "lotus-dbus.h"
#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"
#include "window_focus_tracker.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>

#include <unistd.h>

namespace {
    constexpr auto    kPollInterval = std::chrono::milliseconds(10);
    constexpr auto    kWaitTimeout  = std::chrono::seconds(4);

    DbusConnectionPtr connectBus() {
        DbusErrorGuard    error;
        DbusConnectionPtr bus(dbus_bus_get_private(DBUS_BUS_SESSION, error.get()), DbusConnectionDeleter{.isPrivate = true});
        if (bus) {
            dbus_connection_set_exit_on_disconnect(bus.get(), FALSE);
        }
        return bus;
    }

    // A real private D-Bus daemon supplies sender identities and process lookup. Only the
    // accessibility registry is mocked; no desktop, user bus or display is consulted.
    class Registry {
      public:
        Registry() : bus_(connectBus()) {
            if (!bus_) {
                return;
            }
            DbusErrorGuard error;
            if (dbus_bus_request_name(bus_.get(), "org.a11y.atspi.Registry", DBUS_NAME_FLAG_DO_NOT_QUEUE, error.get()) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                return;
            }
            thread_ = std::thread([this] {
                while (!stop_.load() && dbus_connection_read_write(bus_.get(), 10) != FALSE) {
                    while (DbusMessagePtr request{dbus_connection_pop_message(bus_.get())}) {
                        if (dbus_message_get_type(request.get()) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
                            continue;
                        }
                        DbusMessagePtr reply(dbus_message_new_method_return(request.get()));
                        if (dbus_message_is_method_call(request.get(), "org.a11y.atspi.Accessible", "GetChildren") != FALSE) {
                            DBusMessageIter body{};
                            DBusMessageIter children{};
                            dbus_message_iter_init_append(reply.get(), &body);
                            dbus_message_iter_open_container(&body, DBUS_TYPE_ARRAY, "(so)", &children);
                            dbus_message_iter_close_container(&body, &children);
                        }
                        dbus_connection_send(bus_.get(), reply.get(), nullptr);
                        dbus_connection_flush(bus_.get());
                    }
                }
            });
        }

        ~Registry() {
            stop_.store(true);
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        Registry(const Registry&)                = delete;
        Registry& operator=(const Registry&)     = delete;
        Registry(Registry&&)                     = delete;
        Registry&          operator=(Registry&&) = delete;

        [[nodiscard]] bool valid() const {
            return thread_.joinable();
        }

      private:
        DbusConnectionPtr bus_;
        std::atomic<bool> stop_{false};
        std::thread       thread_;
    };

    void emitEvent(const DbusConnectionPtr& bus, const char* path, const char* interface, const char* member, const char* name, dbus_int32_t state) {
        DbusMessagePtr message(dbus_message_new_signal(path, interface, member));
        dbus_int32_t   zero = 0;
        dbus_message_append_args(message.get(), DBUS_TYPE_STRING, &name, DBUS_TYPE_INT32, &state, DBUS_TYPE_INT32, &zero, DBUS_TYPE_INVALID);
        DBusMessageIter body{};
        DBusMessageIter data{};
        DBusMessageIter properties{};
        dbus_message_iter_init_append(message.get(), &body);
        dbus_message_iter_open_container(&body, DBUS_TYPE_VARIANT, "i", &data);
        dbus_message_iter_append_basic(&data, DBUS_TYPE_INT32, &zero);
        dbus_message_iter_close_container(&body, &data);
        dbus_message_iter_open_container(&body, DBUS_TYPE_ARRAY, "{sv}", &properties);
        dbus_message_iter_close_container(&body, &properties);
        dbus_connection_send(bus.get(), message.get(), nullptr);
        dbus_connection_flush(bus.get());
    }

    void emitFocus(const DbusConnectionPtr& bus, const char* path, bool focused) {
        emitEvent(bus, path, "org.a11y.atspi.Event.Object", "StateChanged", "focused", focused ? 1 : 0);
    }

    void emitWindow(const DbusConnectionPtr& bus, const char* path, const char* event) {
        emitEvent(bus, path, "org.a11y.atspi.Event.Window", event, "", 0);
    }

    bool check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
        }
        return condition;
    }
} // namespace

int main(int argc, char* argv[]) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    if (argc == 1) {
        // Re-exec before constructing Fcitx/libdbus objects, isolating all bus traffic from
        // the user's session. Use our own configuration so sandboxes need no host session
        // configuration or services. dbus-run-session tears down its daemon on every exit path.
        execlp("dbus-run-session", "dbus-run-session", "--config-file", LOTUS_TEST_DBUS_CONFIG, "--", arguments.front(), "--private-bus", nullptr);
        std::perror("dbus-run-session");
        return 1;
    }
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!check("private bus address exists", address != nullptr)) {
        return 1;
    }
    setenv("AT_SPI_BUS_ADDRESS", address, 1);
    setenv("WAYLAND_DISPLAY", "lotus-focus-test", 1);
    configureTestPaths("fcitx5-lotus-window-focus-tracker");
    dbusEnsureThreadsInitialised();
    Registry registry;
    if (!check("private accessibility registry started", registry.valid())) {
        return 1;
    }

    const std::string program   = programForPid(getpid());
    const auto        rulesFile = std::filesystem::temp_directory_path() / "fcitx5-lotus-window-focus-tracker/config/fcitx5/conf/lotus-app-rules.conf";
    {
        std::ofstream file(rulesFile);
        file << program << "=0\n";
    }

    TestInstance       instance;
    fcitx::LotusEngine engine(&instance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("ModeMenuKey/0", "asciitilde");
    engine.setConfig(config);
    auto context = std::make_unique<TestInputContext>(&instance.instance, "gnome-shell");
    context->setFrontendName("ibus");
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    fcitx::WindowFocusTracker tracker;
    tracker.start();
    auto application       = connectBus();
    auto unrelated         = connectBus();
    auto windowApplication = connectBus();
    if (!check("mock application connections opened", application != nullptr && unrelated != nullptr && windowApplication != nullptr)) {
        return 1;
    }
    auto&      loop         = instance.instance.eventLoop();
    int        phase        = 0;
    int        result       = 0;
    auto       phaseStarted = std::chrono::steady_clock::now();
    const auto expect       = [&](const char* name, bool condition) {
        if (!check(name, condition)) {
            result = 1;
            loop.exit();
        }
        return condition;
    };
    const auto advance = [&] {
        ++phase;
        phaseStarted = std::chrono::steady_clock::now();
    };
    const auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0, [&](fcitx::EventSourceTime* source, uint64_t) {
        if (!expect("accessibility focus phase completed before timeout", std::chrono::steady_clock::now() - phaseStarted < kWaitTimeout)) {
            std::printf("Timed out in focus phase %d\n", phase);
            return false;
        }
        switch (phase) {
            case 0:
                // Retry only while subscriptions are starting. Later cases emit a single event.
                emitFocus(application, "/field/first", true);
                if (tracker.focusedProgram() == program && g_realMode.load() == fcitx::LotusMode::Off) {
                    advance();
                }
                break;
            case 1: {
                if (std::chrono::steady_clock::now() - phaseStarted < std::chrono::milliseconds(1200)) {
                    break;
                }
                if (!expect("idle focus survives the former one-second expiry", tracker.focusedProgram() == program)) {
                    return false;
                }
                fcitx::KeyEvent menu(context.get(), fcitx::Key(FcitxKey_asciitilde), false);
                engine.keyEvent(entry, menu);
                if (!expect("idle mode menu shows the real application", context->inputPanel().auxDown().toString() == "App: " + program)) {
                    return false;
                }
                fcitx::KeyEvent escape(context.get(), fcitx::Key(FcitxKey_Escape), false);
                engine.keyEvent(entry, escape);
                emitFocus(application, "/field/second", true);
                emitFocus(application, "/field/first", false);
                emitFocus(unrelated, "/field/second", false);
                advance();
                break;
            }
            case 2:
                if (std::chrono::steady_clock::now() - phaseStarted < std::chrono::milliseconds(200)) {
                    break;
                }
                if (!expect("late and unrelated blur events preserve the current node", tracker.focusedProgram() == program)) {
                    return false;
                }
                emitFocus(application, "/field/second", false);
                advance();
                break;
            case 3: {
                if (tracker.focusedProgram() || g_realMode.load() != fcitx::LotusMode::Preedit) {
                    break;
                }
                fcitx::KeyEvent composing(context.get(), fcitx::Key(FcitxKey_a), false);
                engine.keyEvent(entry, composing);
                if (!expect("unknown shared context has an unfinished composition", !context->inputPanel().clientPreedit().empty())) {
                    return false;
                }
                emitFocus(application, "/field/first", true);
                advance();
                break;
            }
            case 4:
                if (tracker.focusedProgram() != program || g_realMode.load() != fcitx::LotusMode::Off) {
                    break;
                }
                if (!expect("old composition is discarded instead of committed into the new application",
                            context->commits().empty() && context->inputPanel().clientPreedit().empty())) {
                    return false;
                }
                application.reset();
                advance();
                break;
            case 5:
                if (!tracker.focusedProgram() && g_realMode.load() == fcitx::LotusMode::Preedit) {
                    // Sublime reports window activation without a focused editor node.
                    emitWindow(windowApplication, "/window/active", "Activate");
                    advance();
                }
                break;
            case 6:
                if (tracker.focusedProgram() == program && g_realMode.load() == fcitx::LotusMode::Off) {
                    emitFocus(windowApplication, "/window/active/field", true);
                    emitFocus(windowApplication, "/window/active/field", false);
                    emitWindow(unrelated, "/window/old", "Deactivate");
                    advance();
                }
                break;
            case 7:
                if (std::chrono::steady_clock::now() - phaseStarted < std::chrono::milliseconds(200)) {
                    break;
                }
                if (!expect("active window survives a field blur and an unrelated window deactivation", tracker.focusedProgram() == program)) {
                    return false;
                }
                emitWindow(windowApplication, "/window/active", "Deactivate");
                advance();
                break;
            case 8:
                if (!tracker.focusedProgram() && g_realMode.load() == fcitx::LotusMode::Preedit) {
                    emitWindow(windowApplication, "/window/active", "Activate");
                    advance();
                }
                break;
            case 9:
                if (tracker.focusedProgram() == program && g_realMode.load() == fcitx::LotusMode::Off) {
                    windowApplication.reset();
                    advance();
                }
                break;
            case 10:
                if (!tracker.focusedProgram() && g_realMode.load() == fcitx::LotusMode::Preedit) {
                    loop.exit();
                    return false;
                }
                break;
            default:
                result = 1;
                loop.exit();
                return false;
        }
        source->setTime(fcitx::now(CLOCK_MONOTONIC) + (static_cast<uint64_t>(kPollInterval.count()) * 1000));
        source->setOneShot();
        return true;
    });
    loop.exec();
    tracker.stop();
    if (result == 0) {
        std::printf("lotus window focus tracker: all checks passed\n");
    }
    return result;
}
