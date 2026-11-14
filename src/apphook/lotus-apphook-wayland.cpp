/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: MIT
 *
 */

/**
 * @file lotus-apphook-wayland.cpp
 * @brief Intercepts Wayland keyboard callbacks, including existing proxies.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-target.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdarg>
#include <cctype>
#include <cstring>
#include <dlfcn.h>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

// Protocol layout and private proxy offsets are taken from idk-overlay's
// libwayland-client input hook. No generated Wayland bindings are required.
struct wl_proxy;
struct wl_display;
struct wl_keyboard;
struct wl_surface;
struct wl_array;
struct wl_event_queue;

struct WlInterface {
    const char* name;
    int         version;
    int         methodCount;
    const void* methods;
    int         eventCount;
    const void* events;
};

struct WlMessage {
    const char*         name;
    const char*         signature;
    const WlInterface** types;
};

union WlArgument {
    int32_t     i;
    uint32_t    u;
    int32_t     f;
    const char* s;
    void*       o;
    uint32_t    n;
    void*       a;
    int32_t     h;
};

struct KeyboardListener {
    void (*keymap)(void*, wl_keyboard*, uint32_t, int32_t, uint32_t);
    void (*enter)(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*);
    void (*leave)(void*, wl_keyboard*, uint32_t, wl_surface*);
    void (*key)(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t);
    void (*modifiers)(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    void (*repeatInfo)(void*, wl_keyboard*, int32_t, int32_t);
};

using Dispatcher = int (*)(const void*, void*, uint32_t, const WlMessage*, WlArgument*);

namespace lotus::apphook {
    namespace {

        constexpr size_t kImplementationOffset = 8;
        constexpr size_t kDataOffset           = 48;
        constexpr size_t kDispatcherOffset     = 56;
        constexpr size_t kDisplayQueueOffset   = 32;
        constexpr size_t kProxyListOffset      = 16;
        constexpr size_t kProxyQueueLinkOffset = 80;

        struct WlList {
            WlList* previous;
            WlList* next;
        };

        struct Keyboard {
            wl_proxy*               proxy          = nullptr;
            wl_display*             display        = nullptr;
            const KeyboardListener* listener       = nullptr;
            const void*             implementation = nullptr;
            void*                   originalData   = nullptr;
            Dispatcher              dispatcher     = nullptr;
            bool                    focused        = false;
            std::atomic<bool>       alive{true};
            uint32_t                serial     = 0;
            uint32_t                time       = 0;
            uint32_t                depressed  = 0;
            uint32_t                latched    = 0;
            uint32_t                locked     = 0;
            uint32_t                group      = 0;
            uint32_t                shiftIndex = 0;
            uint32_t                ctrlIndex  = 2;
            uint32_t                altIndex   = 3;
            uint32_t                superIndex = 6;
        };

        std::list<std::shared_ptr<Keyboard>> keyboards;
        std::recursive_mutex                 keyboardMutex;
        std::mutex                           waylandConnectionMutex;
        void*                                wayland = nullptr;
        void*                                xkb     = nullptr;
        std::mutex                           resolveMutex;

        int (*originalAddListener)(wl_proxy*, void (**)(void), void*)                                                    = nullptr;
        int (*originalAddDispatcher)(wl_proxy*, Dispatcher, const void*, void*)                                          = nullptr;
        int (*originalDispatchQueuePending)(wl_display*, wl_event_queue*)                                                = nullptr;
        int (*originalDispatchPending)(wl_display*)                                                                      = nullptr;
        int (*originalDispatch)(wl_display*)                                                                             = nullptr;
        int (*originalDispatchQueue)(wl_display*, wl_event_queue*)                                                       = nullptr;
        wl_display* (*originalConnect)(const char*)                                                                      = nullptr;
        wl_display* (*originalConnectFd)(int)                                                                            = nullptr;
        void (*originalDestroy)(wl_proxy*)                                                                               = nullptr;
        void (*originalDisconnect)(wl_display*)                                                                          = nullptr;
        wl_event_queue* (*originalGetQueue)(const wl_proxy*)                                                             = nullptr;
        void (*originalSetQueue)(wl_proxy*, wl_event_queue*)                                                             = nullptr;
        wl_proxy* (*originalMarshalArrayFlags)(wl_proxy*, uint32_t, const WlInterface*, uint32_t, uint32_t, WlArgument*) = nullptr;
        wl_display* (*originalGetProxyDisplay)(wl_proxy*)                                                                = nullptr;
        const char* (*originalClass)(wl_proxy*)                                                                          = nullptr;
        int (*originalFlush)(wl_display*)                                                                                = nullptr;
        wl_proxy* (*originalMarshalConstructor)(wl_proxy*, uint32_t, const WlInterface*, uint32_t, ...)                  = nullptr;
        const WlInterface* callbackInterface                                                                             = nullptr;

        void* (*newXkbContext)(int)                         = nullptr;
        void (*releaseXkbContext)(void*)                    = nullptr;
        void* (*newXkbKeymap)(void*, const char*, int, int) = nullptr;
        void (*releaseXkbKeymap)(void*)                     = nullptr;
        uint32_t (*lookupXkbModifier)(void*, const char*)   = nullptr;

        /**
         * @brief Reads a field from an existing Wayland proxy.
         * @param proxy Wayland proxy address.
         * @param offset Offset of the private field.
         * @return Field value.
         */
        template <typename Type>
        Type proxyField(wl_proxy* proxy, size_t offset) {
            return *reinterpret_cast<Type*>(reinterpret_cast<char*>(proxy) + offset);
        }

        /**
         * @brief Writes a field into an existing Wayland proxy.
         * @param proxy Wayland proxy address.
         * @param offset Offset of the private field.
         * @param value New field value.
         */
        template <typename Type>
        void setProxyField(wl_proxy* proxy, size_t offset, Type value) {
            *reinterpret_cast<Type*>(reinterpret_cast<char*>(proxy) + offset) = value;
        }

        /**
         * @brief Resolves a Wayland or XKB function address.
         * @param handle Loaded shared library handle.
         * @param name Dynamic symbol name.
         * @return Typed function address.
         */
        template <typename Type>
        Type resolve(void* handle, const char* name) {
            return reinterpret_cast<Type>(dlsym(handle, name));
        }

        /**
         * @brief Finds an intercepted keyboard by its proxy.
         * @param proxy Wayland keyboard proxy.
         * @return Tracked keyboard, or null if absent.
         */
        Keyboard* lookupKeyboard(wl_proxy* proxy) {
            std::scoped_lock lock(keyboardMutex);
            const auto       it = std::find_if(keyboards.begin(), keyboards.end(), [&](const auto& keyboard) { return keyboard->proxy == proxy; });
            return it == keyboards.end() ? nullptr : it->get();
        }

        /**
         * @brief Finds the keyboard currently focused by the application.
         * @return Focused keyboard, or null if none.
         */
        Keyboard* focusedKeyboard(wl_display* display = nullptr) {
            std::scoped_lock lock(keyboardMutex);
            const auto       it = std::find_if(keyboards.begin(), keyboards.end(),
                                               [display](const auto& keyboard) { return keyboard->focused && (!display || !keyboard->display || keyboard->display == display); });
            return it == keyboards.end() ? nullptr : it->get();
        }

        std::shared_ptr<Keyboard> retainKeyboard(Keyboard* keyboard) {
            std::scoped_lock lock(keyboardMutex);
            const auto       it = std::find_if(keyboards.begin(), keyboards.end(), [keyboard](const auto& item) { return item.get() == keyboard; });
            return it == keyboards.end() ? nullptr : *it;
        }

        // Keep callback data alive during reentrant wl_keyboard_release/destroy.
        void forgetProxy(wl_proxy* proxy) {
            bool changed   = false;
            bool available = false;
            bool focused   = false;
            {
                std::scoped_lock lock(keyboardMutex);
                for (auto it = keyboards.begin(); it != keyboards.end();) {
                    if ((*it)->proxy == proxy) {
                        (*it)->alive   = false;
                        (*it)->focused = false;
                        it             = keyboards.erase(it);
                        changed        = true;
                    } else {
                        ++it;
                    }
                }
                available = !keyboards.empty();
                focused   = focusedKeyboard() != nullptr;
            }
            if (changed) {
                noteWaylandKeyboardAvailable(available);
                noteWaylandFocus(focused);
            }
        }

        /**
         * @brief Resolves optional XKB keymap symbols for modifier mapping.
         */
        void loadXkb() {
            if (xkb)
                return;
            xkb = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
            if (!xkb)
                return;
            newXkbContext     = resolve<decltype(newXkbContext)>(xkb, "xkb_context_new");
            releaseXkbContext = resolve<decltype(releaseXkbContext)>(xkb, "xkb_context_unref");
            newXkbKeymap      = resolve<decltype(newXkbKeymap)>(xkb, "xkb_keymap_new_from_string");
            releaseXkbKeymap  = resolve<decltype(releaseXkbKeymap)>(xkb, "xkb_keymap_unref");
            lookupXkbModifier = resolve<decltype(lookupXkbModifier)>(xkb, "xkb_keymap_mod_get_index");
        }

        /**
         * @brief Reads the target keyboard keymap to resolve modifier indices.
         * @param keyboard Tracked keyboard state.
         * @param fd Keymap file descriptor supplied by Wayland.
         * @param size Keymap byte size.
         */
        void readKeymap(Keyboard* keyboard, int fd, uint32_t size) {
            if (fd < 0 || size == 0 || size > (1U << 20))
                return;
            loadXkb();
            if (!newXkbContext || !newXkbKeymap || !lookupXkbModifier)
                return;
            void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapping == MAP_FAILED) {
                return;
            }
            const auto                             unmap = [size](void* address) { munmap(address, size); };
            std::unique_ptr<void, decltype(unmap)> ownedMapping(mapping, unmap);
            std::string                            keymap(static_cast<const char*>(ownedMapping.get()), size);
            void*                                  context = newXkbContext(0);
            if (!context)
                return;
            void* map = newXkbKeymap(context, keymap.c_str(), 1, 0);
            if (map) {
                keyboard->shiftIndex = lookupXkbModifier(map, "Shift");
                keyboard->ctrlIndex  = lookupXkbModifier(map, "Control");
                keyboard->altIndex   = lookupXkbModifier(map, "Mod1");
                keyboard->superIndex = lookupXkbModifier(map, "Mod4");
                if (releaseXkbKeymap)
                    releaseXkbKeymap(map);
            }
            if (releaseXkbContext)
                releaseXkbContext(context);
        }

        /**
         * @brief Forwards an event to the original listener or dispatcher.
         * @param keyboard Tracked keyboard state.
         * @param opcode Wayland keyboard event opcode.
         * @param args Wire-format callback arguments.
         */
        void deliver(Keyboard* keyboard, uint32_t opcode, WlArgument* args) {
            if (!keyboard)
                return;
            if (keyboard->dispatcher) {
                static const std::array<WlMessage, 6> messages{{
                    {"keymap", "uhu", nullptr},
                    {"enter", "uoa", nullptr},
                    {"leave", "uo", nullptr},
                    {"key", "uuuu", nullptr},
                    {"modifiers", "uuuuu", nullptr},
                    {"repeat_info", "ii", nullptr},
                }};
                (void)keyboard->dispatcher(keyboard->implementation, keyboard->originalData, opcode, &messages[opcode], args);
                return;
            }
            const auto* listener = keyboard->listener;
            if (!listener)
                return;
            auto* proxy = reinterpret_cast<wl_keyboard*>(keyboard->proxy);
            switch (opcode) {
                case 0:
                    if (listener->keymap)
                        listener->keymap(keyboard->originalData, proxy, args[0].u, args[1].h, args[2].u);
                    break;
                case 1:
                    if (listener->enter)
                        listener->enter(keyboard->originalData, proxy, args[0].u, static_cast<wl_surface*>(args[1].o), static_cast<wl_array*>(args[2].a));
                    break;
                case 2:
                    if (listener->leave)
                        listener->leave(keyboard->originalData, proxy, args[0].u, static_cast<wl_surface*>(args[1].o));
                    break;
                case 3:
                    if (listener->key)
                        listener->key(keyboard->originalData, proxy, args[0].u, args[1].u, args[2].u, args[3].u);
                    break;
                case 4:
                    if (listener->modifiers)
                        listener->modifiers(keyboard->originalData, proxy, args[0].u, args[1].u, args[2].u, args[3].u, args[4].u);
                    break;
                case 5:
                    if (listener->repeatInfo)
                        listener->repeatInfo(keyboard->originalData, proxy, args[0].i, args[1].i);
                    break;
                default: break;
            }
        }

        /**
         * @brief Updates the tracked focus, serial, key time and modifiers.
         * @param keyboard Tracked keyboard state.
         * @param opcode Wayland keyboard event opcode.
         * @param args Observed event arguments.
         */
        void observe(Keyboard* keyboard, uint32_t opcode, const WlArgument* args) {
            std::scoped_lock lock(keyboardMutex);
            if (opcode == 1) {
                for (auto& other : keyboards) {
                    if (other->display == keyboard->display)
                        other->focused = false;
                }
                keyboard->focused = true;
                keyboard->serial  = args[0].u;
                if (keyboard->display)
                    runtime().waylandDisplay.store(keyboard->display, std::memory_order_release);
            } else if (opcode == 2) {
                keyboard->focused = false;
            } else if (opcode == 3) {
                // A key is not an enter: do not invent focus before the
                // actual wl_keyboard.enter has reached this listener.
                keyboard->serial = args[0].u;
                keyboard->time   = args[1].u;
            } else if (opcode == 4) {
                keyboard->serial    = args[0].u;
                keyboard->depressed = args[1].u;
                keyboard->latched   = args[2].u;
                keyboard->locked    = args[3].u;
                keyboard->group     = args[4].u;
            }
            if (opcode >= 1 && opcode <= 3) {
                noteWaylandFocus(focusedKeyboard() != nullptr);
            }
        }

        /**
         * @brief Intercepts a Wayland keymap callback and forwards it.
         * @param data Tracked keyboard user data.
         * @param format Keymap format.
         * @param fd Keymap file descriptor.
         * @param size Keymap size in bytes.
         */
        void onKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (!retained || !retained->alive)
                return;

            auto*      keyboard = retained.get();
            WlArgument args[3]{};
            args[0].u = format;
            args[1].h = fd;
            args[2].u = size;
            if (format == 1)
                readKeymap(keyboard, fd, size);
            deliver(keyboard, 0, args);
        }

        /**
         * @brief Tracks keyboard focus entry and forwards the event.
         * @param data Tracked keyboard user data.
         * @param serial Compositor input serial.
         * @param surface Entered surface.
         * @param keys Pressed keys supplied by the compositor.
         */
        void onEnter(void* data, wl_keyboard*, uint32_t serial, wl_surface* surface, wl_array* keys) {
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (!retained || !retained->alive)
                return;

            auto*      keyboard = retained.get();
            WlArgument args[3]{};
            args[0].u = serial;
            args[1].o = surface;
            args[2].a = keys;
            observe(keyboard, 1, args);
            deliver(keyboard, 1, args);
        }

        /**
         * @brief Tracks keyboard focus exit and forwards the event.
         * @param data Tracked keyboard user data.
         * @param serial Compositor input serial.
         * @param surface Exited surface.
         */
        void onLeave(void* data, wl_keyboard*, uint32_t serial, wl_surface* surface) {
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (!retained || !retained->alive)
                return;

            auto*      keyboard = retained.get();
            WlArgument args[2]{};
            args[0].u = serial;
            args[1].o = surface;
            observe(keyboard, 2, args);
            deliver(keyboard, 2, args);
        }

        /**
         * @brief Tracks a physical key event and forwards it.
         * @param data Tracked keyboard user data.
         * @param serial Compositor input serial.
         * @param time Event time.
         * @param key Evdev key code.
         * @param state Key press or release state.
         */
        void onKey(void* data, wl_keyboard*, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (!retained || !retained->alive)
                return;

            auto*      keyboard = retained.get();
            WlArgument args[4]{};
            args[0].u = serial;
            args[1].u = time;
            args[2].u = key;
            args[3].u = state;
            observe(keyboard, 3, args);
            if (state == 1 && keyboard->focused) {
                noteKeyboardActivity(true);
            }
            deliver(keyboard, 3, args);
        }

        /**
         * @brief Tracks the current modifier state and forwards it.
         * @param data Tracked keyboard user data.
         * @param serial Compositor input serial.
         * @param depressed Depressed modifiers.
         * @param latched Latched modifiers.
         * @param locked Locked modifiers.
         * @param group Keyboard layout group.
         */
        void onModifiers(void* data, wl_keyboard*, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (!retained || !retained->alive)
                return;

            auto*      keyboard = retained.get();
            WlArgument args[5]{};
            args[0].u = serial;
            args[1].u = depressed;
            args[2].u = latched;
            args[3].u = locked;
            args[4].u = group;
            observe(keyboard, 4, args);
            deliver(keyboard, 4, args);
        }

        /**
         * @brief Forwards keyboard key repeat settings.
         * @param data Tracked keyboard user data.
         * @param rate Repeat rate.
         * @param delay Initial repeat delay.
         */
        void onRepeat(void* data, wl_keyboard*, int32_t rate, int32_t delay) {
            WlArgument args[2]{};
            args[0].i     = rate;
            args[1].i     = delay;
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (retained && retained->alive)
                deliver(retained.get(), 5, args);
        }

        const KeyboardListener wrapper{onKeymap, onEnter, onLeave, onKey, onModifiers, onRepeat};

        /**
         * @brief Intercepts dispatcher-style Wayland keyboard callbacks.
         * @param implementation Original dispatcher implementation data.
         * @param data Tracked keyboard state.
         * @param opcode Keyboard event opcode.
         * @param message Wire event description.
         * @param args Event arguments.
         * @return Original dispatcher return value.
         */
        int dispatchWrapper(const void* implementation, void* data, uint32_t opcode, const WlMessage* message, WlArgument* args) {
            auto retained = retainKeyboard(static_cast<Keyboard*>(data));
            if (!retained || !retained->alive)
                return 0;
            auto* keyboard = retained.get();
            if (opcode == 0 && args && args[0].u == 1)
                readKeymap(keyboard, args[1].h, args[2].u);
            if (opcode >= 1 && opcode <= 4 && args)
                observe(keyboard, opcode, args);
            if (opcode == 3 && args && args[3].u == 1 && keyboard->focused) {
                noteKeyboardActivity(true);
            }
            return keyboard->dispatcher ? keyboard->dispatcher(implementation, keyboard->originalData, opcode, message, args) : 0;
        }

        /**
         * @brief Replaces an existing keyboard proxy listener/dispatcher with a wrapper.
         * @param proxy Wayland keyboard proxy.
         */
        void wrapProxy(wl_proxy* proxy) {
            if (!proxy || lookupKeyboard(proxy))
                return;
            std::scoped_lock lock(keyboardMutex);
            auto             keyboard = std::make_shared<Keyboard>();
            keyboard->proxy           = proxy;
            keyboard->display         = originalGetProxyDisplay ? originalGetProxyDisplay(proxy) : nullptr;
            keyboard->implementation  = proxyField<void*>(proxy, kImplementationOffset);
            keyboard->originalData    = proxyField<void*>(proxy, kDataOffset);
            keyboard->dispatcher      = proxyField<Dispatcher>(proxy, kDispatcherOffset);
            if (!keyboard->implementation && !keyboard->dispatcher)
                return;
            if (keyboard->dispatcher) {
                setProxyField(proxy, kDispatcherOffset, &dispatchWrapper);
            } else {
                keyboard->listener = static_cast<const KeyboardListener*>(keyboard->implementation);
                setProxyField(proxy, kImplementationOffset, static_cast<const void*>(&wrapper));
            }
            setProxyField(proxy, kDataOffset, static_cast<void*>(keyboard.get()));
            keyboards.push_back(std::move(keyboard));
        }

        /**
         * @brief Retroactively wraps keyboards on an existing Wayland event queue.
         * @param display Application Wayland display.
         */
        void scanProxies(wl_display* display) {
            if (!display || !originalClass)
                return;
            auto* queue = *reinterpret_cast<void**>(reinterpret_cast<char*>(display) + kDisplayQueueOffset);
            if (!queue)
                return;
            auto*    head    = reinterpret_cast<WlList*>(reinterpret_cast<char*>(queue) + kProxyListOffset);
            unsigned visited = 0;
            for (WlList* node = head->next; node && node != head && visited < 512; ++visited) {
                WlList*     next  = node->next;
                auto*       proxy = reinterpret_cast<wl_proxy*>(reinterpret_cast<char*>(node) - kProxyQueueLinkOffset);
                const char* name  = originalClass(proxy);
                if (name && std::strcmp(name, "wl_keyboard") == 0)
                    wrapProxy(proxy);
                node = next;
            }
        }

        /**
         * @brief Converts Lotus modifier bits to the target keyboard keymap mask.
         * @param keyboard Tracked keyboard state.
         * @param modifiers Lotus modifier bitmask.
         * @return Wayland depressed modifier bits.
         */
        uint32_t modifierMask(const Keyboard& keyboard, uint32_t modifiers) {
            uint32_t mask = 0;
            if ((modifiers & kModifierShift) && keyboard.shiftIndex < 32)
                mask |= 1U << keyboard.shiftIndex;
            if ((modifiers & kModifierCtrl) && keyboard.ctrlIndex < 32)
                mask |= 1U << keyboard.ctrlIndex;
            if ((modifiers & kModifierAlt) && keyboard.altIndex < 32)
                mask |= 1U << keyboard.altIndex;
            if ((modifiers & kModifierSuper) && keyboard.superIndex < 32)
                mask |= 1U << keyboard.superIndex;
            return mask;
        }

        /**
         * @brief Delivers a synthetic modifier state event.
         * @param keyboard Focused keyboard.
         * @param depressed Depressed modifier bits.
         */
        void emitModifiers(Keyboard* keyboard, uint32_t depressed) {
            WlArgument args[5]{};
            args[0].u = keyboard->serial;
            args[1].u = depressed;
            args[2].u = keyboard->latched;
            args[3].u = keyboard->locked;
            args[4].u = keyboard->group;
            deliver(keyboard, 4, args);
        }

        /**
         * @brief Delivers one synthetic Wayland keyboard key edge.
         * @param keyboard Focused keyboard.
         * @param keycode Evdev key code.
         * @param pressed One for press, zero for release.
         */
        void emitKey(Keyboard* keyboard, uint32_t keycode, uint32_t pressed) {
            WlArgument args[4]{};
            args[0].u = keyboard->serial;
            args[1].u = ++keyboard->time;
            args[2].u = keycode;
            args[3].u = pressed;
            deliver(keyboard, 3, args);
        }

        /**
         * @brief Delivers queued synthetic key taps to the focused Wayland keyboard.
         */
        void pump(wl_display* display, wl_event_queue* queue) {
            std::shared_ptr<Keyboard> keyboard;
            {
                std::scoped_lock lock(keyboardMutex);
                Keyboard*        focused = focusedKeyboard(display);
                if (focused && focused->alive && originalGetQueue && originalGetQueue(focused->proxy) == queue)
                    keyboard = retainKeyboard(focused);
            }
            if (!keyboard)
                return;
            auto task = takeTask();
            if (!task)
                return;
            const uint32_t prior = keyboard->depressed;
            for (const KeyOperation& op : task->ops) {
                if (!keyboard->alive || task->cancelled.load(std::memory_order_acquire))
                    break;
                const uint32_t combined = prior | modifierMask(*keyboard, op.modifiers);
                if (combined != prior)
                    emitModifiers(keyboard.get(), combined);
                for (uint32_t i = 0; i < op.count && keyboard->alive && !task->cancelled.load(std::memory_order_acquire); ++i) {
                    task->applied.store(true, std::memory_order_release);
                    emitKey(keyboard.get(), op.keycode, 1);
                    if (keyboard->alive)
                        emitKey(keyboard.get(), op.keycode, 0);
                }
                if (combined != prior && keyboard->alive)
                    emitModifiers(keyboard.get(), prior);
            }
            std::scoped_lock lock(runtime().mutex);
            finishLocked(task, !keyboard->alive ? -ENOTCONN : task->cancelled.load(std::memory_order_acquire) ? -ECANCELED : 0);
        }

        /**
         * @brief Attaches existing proxies and processes keys only after queued Wayland events.
         * @param display Current Wayland display.
         * @param completed Whether the original dispatch has returned successfully.
         */
        void onDispatch(wl_display* display, wl_event_queue* queue, bool completed) {
            if (display) {
                wl_display* expected = nullptr;
                (void)runtime().waylandDisplay.compare_exchange_strong(expected, display, std::memory_order_acq_rel);
            }
            if (runtime().lateAttached.load(std::memory_order_acquire))
                scanProxies(runtime().waylandDisplay.load(std::memory_order_acquire));
            if (completed && runtime().workersStarted.load(std::memory_order_acquire))
                pump(display, queue);
        }

        /**
         * @brief Destroys the internal wakeup callback proxy.
         * @param callback Completed callback proxy.
         */
        void callbackDone(void*, wl_proxy* callback, uint32_t) {
            if (originalDestroy)
                originalDestroy(callback);
        }

    } // namespace

    void loadWayland() {
        std::scoped_lock lock(resolveMutex);
        if (wayland)
            return;
        wayland = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD);
        if (!wayland)
            return;
        originalAddListener          = resolve<decltype(originalAddListener)>(wayland, "wl_proxy_add_listener");
        originalAddDispatcher        = resolve<decltype(originalAddDispatcher)>(wayland, "wl_proxy_add_dispatcher");
        originalDispatchQueuePending = resolve<decltype(originalDispatchQueuePending)>(wayland, "wl_display_dispatch_queue_pending");
        originalDispatchPending      = resolve<decltype(originalDispatchPending)>(wayland, "wl_display_dispatch_pending");
        originalDispatch             = resolve<decltype(originalDispatch)>(wayland, "wl_display_dispatch");
        originalDispatchQueue        = resolve<decltype(originalDispatchQueue)>(wayland, "wl_display_dispatch_queue");
        originalConnect              = resolve<decltype(originalConnect)>(wayland, "wl_display_connect");
        originalConnectFd            = resolve<decltype(originalConnectFd)>(wayland, "wl_display_connect_to_fd");
        originalDestroy              = resolve<decltype(originalDestroy)>(wayland, "wl_proxy_destroy");
        originalDisconnect           = resolve<decltype(originalDisconnect)>(wayland, "wl_display_disconnect");
        originalGetQueue             = resolve<decltype(originalGetQueue)>(wayland, "wl_proxy_get_queue");
        originalSetQueue             = resolve<decltype(originalSetQueue)>(wayland, "wl_proxy_set_queue");
        originalMarshalArrayFlags    = resolve<decltype(originalMarshalArrayFlags)>(wayland, "wl_proxy_marshal_array_flags");
        originalGetProxyDisplay      = resolve<decltype(originalGetProxyDisplay)>(wayland, "wl_proxy_get_display");
        originalClass                = resolve<decltype(originalClass)>(wayland, "wl_proxy_get_class");
        originalFlush                = resolve<decltype(originalFlush)>(wayland, "wl_display_flush");
        originalMarshalConstructor   = resolve<decltype(originalMarshalConstructor)>(wayland, "wl_proxy_marshal_constructor_versioned");
        callbackInterface            = resolve<const WlInterface*>(wayland, "wl_callback_interface");
    }

    void wakeWayland() {
        std::scoped_lock connectionLock(waylandConnectionMutex);
        wl_display*      display = nullptr;
        if (!originalMarshalConstructor || !originalFlush || !originalAddListener || !originalSetQueue || !originalGetQueue || !callbackInterface)
            return;
        wl_event_queue* queue = nullptr;
        {
            std::scoped_lock lock(keyboardMutex);
            Keyboard*        keyboard = focusedKeyboard();
            if (!keyboard || !keyboard->alive || !keyboard->display)
                return;
            display = keyboard->display;
            queue   = originalGetQueue(keyboard->proxy);
        }
        if (!queue)
            return;
        wl_proxy* callback = originalMarshalConstructor(reinterpret_cast<wl_proxy*>(display), 0, callbackInterface, 1);
        if (!callback)
            return;
        originalSetQueue(callback, queue);
        static void (*const listener[])(void) = {reinterpret_cast<void (*)(void)>(&callbackDone)};
        if (originalAddListener(callback, const_cast<void (**)(void)>(listener), nullptr) != 0) {
            if (originalDestroy)
                originalDestroy(callback);
            return;
        }
        originalFlush(display);
    }

} // namespace lotus::apphook

#define LH_EXPORT extern "C" __attribute__((visibility("default")))

/**
 * @brief Intercepts listener registration and wraps newly registered keyboards.
 * @param proxy Target Wayland proxy.
 * @param callbacks Listener callback table.
 * @param data Original listener user data.
 * @return Original registration status.
 */
LH_EXPORT int wl_proxy_add_listener(wl_proxy* proxy, void (**callbacks)(void), void* data) {
    using namespace lotus::apphook;
    if (!originalAddListener)
        loadWayland();
    if (!originalAddListener)
        return -1;
    if (!proxy || !callbacks || !originalClass || std::strcmp(originalClass(proxy), "wl_keyboard") != 0 || lookupKeyboard(proxy))
        return originalAddListener(proxy, callbacks, data);

    std::scoped_lock lock(keyboardMutex);
    auto             keyboard = std::make_shared<Keyboard>();
    keyboard->proxy           = proxy;
    keyboard->display         = originalGetProxyDisplay ? originalGetProxyDisplay(proxy) : nullptr;
    keyboard->listener        = reinterpret_cast<const KeyboardListener*>(callbacks);
    keyboard->originalData    = data;
    Keyboard* tracked         = keyboard.get();
    keyboards.push_back(std::move(keyboard));
    const int result = originalAddListener(proxy, reinterpret_cast<void (**)(void)>(const_cast<KeyboardListener*>(&wrapper)), tracked);
    if (result != 0)
        keyboards.pop_back();
    else
        noteWaylandKeyboardAvailable(true);
    return result;
}

/**
 * @brief Intercepts dispatcher registration and wraps newly registered keyboards.
 * @param proxy Target Wayland proxy.
 * @param dispatcher Original dispatcher.
 * @param implementation Original dispatcher implementation.
 * @param data Original dispatcher user data.
 * @return Original registration status.
 */
LH_EXPORT int wl_proxy_add_dispatcher(wl_proxy* proxy, Dispatcher dispatcher, const void* implementation, void* data) {
    using namespace lotus::apphook;
    if (!originalAddDispatcher)
        loadWayland();
    if (!originalAddDispatcher)
        return -1;
    if (!proxy || !dispatcher || !originalClass || std::strcmp(originalClass(proxy), "wl_keyboard") != 0 || lookupKeyboard(proxy))
        return originalAddDispatcher(proxy, dispatcher, implementation, data);

    std::scoped_lock lock(keyboardMutex);
    auto             keyboard = std::make_shared<Keyboard>();
    keyboard->proxy           = proxy;
    keyboard->display         = originalGetProxyDisplay ? originalGetProxyDisplay(proxy) : nullptr;
    keyboard->dispatcher      = dispatcher;
    keyboard->implementation  = implementation;
    keyboard->originalData    = data;
    Keyboard* tracked         = keyboard.get();
    keyboards.push_back(std::move(keyboard));
    const int result = originalAddDispatcher(proxy, &dispatchWrapper, implementation, tracked);
    if (result != 0)
        keyboards.pop_back();
    else
        noteWaylandKeyboardAvailable(true);
    return result;
}

/** Forward Wayland's variadic marshaller through the equivalent argument-array API. */
LH_EXPORT wl_proxy* wl_proxy_marshal_flags(wl_proxy* proxy, uint32_t opcode, const WlInterface* interface, uint32_t version, uint32_t flags, ...) {
    using namespace lotus::apphook;
    if (!originalMarshalArrayFlags)
        loadWayland();
    if (!originalMarshalArrayFlags || !proxy)
        return nullptr;
    const auto* protocol = proxyField<const WlInterface*>(proxy, 0);
    if (!protocol || opcode >= static_cast<uint32_t>(protocol->methodCount))
        return nullptr;
    const auto* methods   = static_cast<const WlMessage*>(protocol->methods);
    const char* signature = methods[opcode].signature;
    WlArgument  arguments[20]{};
    unsigned    count = 0;
    va_list     ap;
    va_start(ap, flags);
    for (const char* p = signature; p && *p && count < 20; ++p) {
        if (*p == '?' || (*p >= '0' && *p <= '9'))
            continue;
        switch (*p) {
            case 'u': arguments[count++].u = va_arg(ap, uint32_t); break;
            case 'i':
            case 'f':
            case 'h': arguments[count++].i = va_arg(ap, int32_t); break;
            case 's': arguments[count++].s = va_arg(ap, const char*); break;
            case 'o':
            case 'n':
            case 'a': arguments[count++].o = va_arg(ap, void*); break;
            default: break;
        }
    }
    va_end(ap);
    const bool destroy = (flags & 1U) != 0;
    // Lifetime stays valid until the real marshaller has finished with the proxy.
    wl_proxy* result = originalMarshalArrayFlags(proxy, opcode, interface, version, flags, arguments);
    if (destroy)
        forgetProxy(proxy);
    return result;
}

/** Array-based marshals use the same destruction flag. */
LH_EXPORT wl_proxy* wl_proxy_marshal_array_flags(wl_proxy* proxy, uint32_t opcode, const WlInterface* interface, uint32_t version, uint32_t flags, WlArgument* args) {
    using namespace lotus::apphook;
    if (!originalMarshalArrayFlags)
        loadWayland();
    if (!originalMarshalArrayFlags)
        return nullptr;
    wl_proxy* result = originalMarshalArrayFlags(proxy, opcode, interface, version, flags, args);
    if (flags & 1U)
        forgetProxy(proxy);
    return result;
}

/** Disconnect invalidates every keyboard and pending wakeup on this display. */
LH_EXPORT void wl_display_disconnect(wl_display* display) {
    using namespace lotus::apphook;
    if (!originalDisconnect)
        loadWayland();
    std::scoped_lock       connectionLock(waylandConnectionMutex);
    std::vector<wl_proxy*> removed;
    {
        std::scoped_lock lock(keyboardMutex);
        for (auto& keyboard : keyboards)
            if (keyboard->display == display)
                removed.push_back(keyboard->proxy);
    }
    for (auto* proxy : removed)
        forgetProxy(proxy);
    if (runtime().waylandDisplay.load(std::memory_order_acquire) == display)
        runtime().waylandDisplay.store(nullptr, std::memory_order_release);
    if (originalDisconnect)
        originalDisconnect(display);
}

/**
 * @brief Drops keyboard tracking when its Wayland proxy is destroyed.
 * @param proxy Proxy being destroyed.
 */
LH_EXPORT void wl_proxy_destroy(wl_proxy* proxy) {
    using namespace lotus::apphook;
    forgetProxy(proxy);
    if (!originalDestroy)
        loadWayland();
    if (originalDestroy)
        originalDestroy(proxy);
}

/**
 * @brief Processes synthetic keyboard events around queued Wayland dispatch.
 * @param display Application Wayland display.
 * @param queue Wayland event queue.
 * @return Original dispatch status.
 */
LH_EXPORT int wl_display_dispatch_queue_pending(wl_display* display, wl_event_queue* queue) {
    using namespace lotus::apphook;
    onDispatch(display, queue, false);
    if (!originalDispatchQueuePending)
        loadWayland();
    const int result = originalDispatchQueuePending ? originalDispatchQueuePending(display, queue) : -1;
    if (result >= 0)
        onDispatch(display, queue, true);
    return result;
}

/**
 * @brief Processes synthetic keyboard events around pending Wayland dispatch.
 * @param display Application Wayland display.
 * @return Original dispatch status.
 */
LH_EXPORT int wl_display_dispatch_pending(wl_display* display) {
    using namespace lotus::apphook;
    onDispatch(display, originalGetQueue ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, false);
    if (!originalDispatchPending)
        loadWayland();
    const int result = originalDispatchPending ? originalDispatchPending(display) : -1;
    if (result >= 0)
        onDispatch(display, originalGetQueue ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, true);
    return result;
}

/**
 * @brief Processes synthetic keyboard events around blocking Wayland dispatch.
 * @param display Application Wayland display.
 * @return Original dispatch status.
 */
LH_EXPORT int wl_display_dispatch(wl_display* display) {
    using namespace lotus::apphook;
    onDispatch(display, originalGetQueue ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, false);
    if (!originalDispatch)
        loadWayland();
    const int result = originalDispatch ? originalDispatch(display) : -1;
    if (result >= 0)
        onDispatch(display, originalGetQueue ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, true);
    return result;
}

/**
 * @brief Processes synthetic keyboard events around blocking queue dispatch.
 * @param display Application Wayland display.
 * @param queue Wayland event queue.
 * @return Original dispatch status.
 */
LH_EXPORT int wl_display_dispatch_queue(wl_display* display, wl_event_queue* queue) {
    using namespace lotus::apphook;
    onDispatch(display, queue, false);
    if (!originalDispatchQueue)
        loadWayland();
    const int result = originalDispatchQueue ? originalDispatchQueue(display, queue) : -1;
    if (result >= 0)
        onDispatch(display, queue, true);
    return result;
}

/**
 * @brief Records a newly connected Wayland display.
 * @param name Wayland display socket name.
 * @return Connected display or null.
 */
LH_EXPORT wl_display* wl_display_connect(const char* name) {
    using namespace lotus::apphook;
    if (!originalConnect)
        loadWayland();
    wl_display* display = originalConnect ? originalConnect(name) : nullptr;
    if (display)
        runtime().waylandDisplay.store(display);
    return display;
}

/**
 * @brief Records a Wayland display connected through a file descriptor.
 * @param fd Connected Wayland socket descriptor.
 * @return Connected display or null.
 */
LH_EXPORT wl_display* wl_display_connect_to_fd(int fd) {
    using namespace lotus::apphook;
    if (!originalConnectFd)
        loadWayland();
    wl_display* display = originalConnectFd ? originalConnectFd(fd) : nullptr;
    if (display)
        runtime().waylandDisplay.store(display);
    return display;
}

namespace lotus::apphook {

    int installWaylandHooks() {
        if (!wayland)
            return 0;
        const std::array hooks{
            HookSpec{"wl_proxy_add_listener", reinterpret_cast<void*>(&wl_proxy_add_listener)},
            HookSpec{"wl_proxy_add_dispatcher", reinterpret_cast<void*>(&wl_proxy_add_dispatcher)},
            HookSpec{"wl_proxy_destroy", reinterpret_cast<void*>(&wl_proxy_destroy)},
            HookSpec{"wl_proxy_marshal_flags", reinterpret_cast<void*>(&wl_proxy_marshal_flags)},
            HookSpec{"wl_proxy_marshal_array_flags", reinterpret_cast<void*>(&wl_proxy_marshal_array_flags)},
            HookSpec{"wl_display_disconnect", reinterpret_cast<void*>(&wl_display_disconnect)},
            HookSpec{"wl_display_dispatch_queue_pending", reinterpret_cast<void*>(&wl_display_dispatch_queue_pending)},
            HookSpec{"wl_display_dispatch_pending", reinterpret_cast<void*>(&wl_display_dispatch_pending)},
            HookSpec{"wl_display_dispatch", reinterpret_cast<void*>(&wl_display_dispatch)},
            HookSpec{"wl_display_dispatch_queue", reinterpret_cast<void*>(&wl_display_dispatch_queue)},
            HookSpec{"wl_display_connect", reinterpret_cast<void*>(&wl_display_connect)},
            HookSpec{"wl_display_connect_to_fd", reinterpret_cast<void*>(&wl_display_connect_to_fd)},
        };
        return installHooks(hooks);
    }

} // namespace lotus::apphook
