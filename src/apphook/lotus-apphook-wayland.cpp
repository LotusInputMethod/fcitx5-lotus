/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: MIT
 *
 */

/**
 * @file lotus-apphook-wayland.cpp
 * @brief Intercepts Wayland keyboard callbacks registered by the application.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-target.h"
#include "lotus-apphook-fd.h"
#include "lotus-apphook-sequence.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <sys/mman.h>
#include <ctime>
#include <unistd.h>

// Minimal public Wayland protocol types; no generated bindings are required.
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

/**
 * @brief Typed read access to a wl_argument array whose event signature is known.
 * The only code that reads the C union or indexes the libwayland-provided array.
 */
class WlArgs {
  public:
    explicit WlArgs(const WlArgument* args) : args_(args) {}
    [[nodiscard]] uint32_t uintAt(size_t index) const {
        return at(index).u; // NOLINT(cppcoreguidelines-pro-type-union-access) wl_argument ABI
    }
    [[nodiscard]] int32_t intAt(size_t index) const {
        return at(index).i; // NOLINT(cppcoreguidelines-pro-type-union-access) wl_argument ABI
    }
    [[nodiscard]] int32_t fdAt(size_t index) const {
        return at(index).h; // NOLINT(cppcoreguidelines-pro-type-union-access) wl_argument ABI
    }
    [[nodiscard]] void* objectAt(size_t index) const {
        return at(index).o; // NOLINT(cppcoreguidelines-pro-type-union-access) wl_argument ABI
    }
    [[nodiscard]] void* arrayAt(size_t index) const {
        return at(index).a; // NOLINT(cppcoreguidelines-pro-type-union-access) wl_argument ABI
    }

  private:
    [[nodiscard]] const WlArgument& at(size_t index) const {
        return args_[index]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) sized by the event signature
    }
    const WlArgument* args_;
};

extern "C" int   lotus_apphook_activate_inherited_keyboard() __attribute__((visibility("hidden")));
extern "C" void* lotus_apphook_native_dlsym(void*, const char*) __attribute__((visibility("hidden")));
namespace lotus::apphook {

    namespace {

        struct NativeForward;
        struct Keyboard {
            wl_proxy*               proxy          = nullptr;
            wl_display*             display        = nullptr;
            const KeyboardListener* listener       = nullptr;
            const void*             implementation = nullptr;
            Dispatcher              dispatcher     = nullptr;
            std::atomic<bool>       focused{false};
            std::atomic<bool>       alive{true};
            NativeForward*          forward        = nullptr;
            wl_surface*             focusedSurface = nullptr;
            uintptr_t               focusToken     = 0;
            uint32_t                serial         = 0;
            uint32_t                time           = 0;
            std::atomic<uint32_t>   depressed{0};
            uint32_t                latched    = 0;
            uint32_t                locked     = 0;
            uint32_t                group      = 0;
            uint32_t                shiftIndex = 0;
            uint32_t                ctrlIndex  = 2;
            uint32_t                altIndex   = 3;
            uint32_t                superIndex = 6;
        };
        // A wrapper installed before fork remains in libwayland's proxy after
        // the child switches to a new apphook generation. Keep the application's
        // callbacks in stable, append-only records so that child forwarding
        // never touches a parent-owned registry or mutex.
        struct NativeForward {
            wl_proxy*               proxy          = nullptr;
            wl_display*             display        = nullptr;
            const KeyboardListener* listener       = nullptr;
            Dispatcher              dispatcher     = nullptr;
            const void*             implementation = nullptr;
            Keyboard*               tracked        = nullptr;
            pid_t                   ownerPid       = 0;
            std::atomic<bool>       alive{true};
            // Atomic snapshots let a child adopt callbacks without reading a
            // parent Keyboard that may have been mid-update at fork time.
            std::atomic<wl_surface*> surface{nullptr};
            std::atomic<uint32_t>    serial{0};
            std::atomic<uint32_t>    time{0};
            std::atomic<uint32_t>    depressed{0};
            std::atomic<uint32_t>    latched{0};
            std::atomic<uint32_t>    locked{0};
            std::atomic<uint32_t>    group{0};
            std::atomic<uint32_t>    modifierIndices{0x06030200U};
            NativeForward*           next = nullptr;
        };
        std::atomic<NativeForward*> forwardHead{nullptr};

        void                        publishForward(NativeForward* record) {
            auto* head   = forwardHead.load(std::memory_order_relaxed);
            record->next = head;
            while (!forwardHead.compare_exchange_weak(head, record, std::memory_order_release, std::memory_order_relaxed))
                record->next = head;
        }

        // Records are never reclaimed while a fork may still retain a wrapper.
        NativeForward* inheritedForward(wl_proxy* proxy) {
            const pid_t pid = getpid();
            for (auto* record = forwardHead.load(std::memory_order_acquire); record != nullptr; record = record->next) {
                if (record->proxy == proxy && record->ownerPid != pid && record->alive.load(std::memory_order_acquire))
                    return record;
            }
            return nullptr;
        }

        /** @brief Recovers the mutable published record behind a dispatcher implementation pointer. */
        NativeForward* publishedForward(const NativeForward* wanted) {
            for (auto* record = forwardHead.load(std::memory_order_acquire); record != nullptr; record = record->next)
                if (record == wanted)
                    return record;
            return nullptr;
        }

        /** @brief Allocates a forwarding record. Records are published and never freed. */
        NativeForward* allocateForward() {
            try {
                return std::make_unique<NativeForward>().release();
            } catch (const std::bad_alloc&) { return nullptr; }
        }

        void retireForward(wl_proxy* proxy) {
            for (auto* record = forwardHead.load(std::memory_order_acquire); record != nullptr; record = record->next) {
                if (record->proxy == proxy)
                    record->alive.store(false, std::memory_order_release);
            }
        }

        void retireDisplay(wl_display* display) {
            for (auto* record = forwardHead.load(std::memory_order_acquire); record != nullptr; record = record->next) {
                if (record->display == display)
                    record->alive.store(false, std::memory_order_release);
            }
        }

        using KeyboardList = std::list<std::shared_ptr<Keyboard>>;
        struct WaylandGeneration {
            KeyboardList           keyboards;
            std::atomic<uintptr_t> nextFocusToken{0};
            // An actual key event disambiguates simultaneously focused seats.
            Keyboard*            lastInputKeyboard = nullptr;
            std::recursive_mutex keyboardMutex;
            std::mutex           waylandConnectionMutex;
            std::mutex           xkbMutex;
            wl_display*          pendingFlushDisplay = nullptr; // guarded by waylandConnectionMutex
        };
        std::atomic<WaylandGeneration*> currentWayland{nullptr};
        WaylandGeneration&              waylandGeneration() {
            auto* current = currentWayland.load(std::memory_order_acquire);
            if (current != nullptr)
                return *current;
            auto               fresh    = std::make_unique<WaylandGeneration>();
            WaylandGeneration* expected = nullptr;
            if (!currentWayland.compare_exchange_strong(expected, fresh.get(), std::memory_order_acq_rel))
                return *expected;
            // Generations are abandoned, never freed, after fork.
            return *fresh.release();
        }
#define keyboards              (waylandGeneration().keyboards)
#define nextFocusToken         (waylandGeneration().nextFocusToken)
#define lastInputKeyboard      (waylandGeneration().lastInputKeyboard)
#define keyboardMutex          (waylandGeneration().keyboardMutex)
#define waylandConnectionMutex (waylandGeneration().waylandConnectionMutex)
#define xkbMutex               (waylandGeneration().xkbMutex)
        void*             wayland = nullptr;
        std::atomic<bool> waylandCapable{false};
        void*             xkb = nullptr;
        std::once_flag    resolveOnce;

        int (*originalAddListener)(wl_proxy*, void (**)(), void*)                                                        = nullptr;
        int (*originalAddDispatcher)(wl_proxy*, Dispatcher, const void*, void*)                                          = nullptr;
        int (*originalDispatchQueuePending)(wl_display*, wl_event_queue*)                                                = nullptr;
        int (*originalDispatchPending)(wl_display*)                                                                      = nullptr;
        int (*originalDispatch)(wl_display*)                                                                             = nullptr;
        int (*originalDispatchQueue)(wl_display*, wl_event_queue*)                                                       = nullptr;
        int (*originalDispatchTimeout)(wl_display*, const timespec*)                                                     = nullptr;
        int (*originalDispatchQueueTimeout)(wl_display*, wl_event_queue*, const timespec*)                               = nullptr;
        int (*originalDispatchPendingSingle)(wl_display*)                                                                = nullptr;
        int (*originalDispatchQueuePendingSingle)(wl_display*, wl_event_queue*)                                          = nullptr;
        int (*originalPrepareReadQueue)(wl_display*, wl_event_queue*)                                                    = nullptr;
        void (*originalCancelRead)(wl_display*)                                                                          = nullptr;
        wl_display* (*originalConnect)(const char*)                                                                      = nullptr;
        int (*originalGetFd)(wl_display*)                                                                                = nullptr;
        wl_display* (*originalConnectFd)(int)                                                                            = nullptr;
        void (*originalDestroy)(wl_proxy*)                                                                               = nullptr;
        void (*originalDisconnect)(wl_display*)                                                                          = nullptr;
        wl_event_queue* (*originalGetQueue)(const wl_proxy*)                                                             = nullptr;
        void (*originalSetQueue)(wl_proxy*, wl_event_queue*)                                                             = nullptr;
        wl_proxy* (*originalMarshalArrayFlags)(wl_proxy*, uint32_t, const WlInterface*, uint32_t, uint32_t, WlArgument*) = nullptr;
        wl_display* (*originalGetProxyDisplay)(wl_proxy*)                                                                = nullptr;
        void* (*originalGetUserData)(wl_proxy*)                                                                          = nullptr;
        using ListenerArray                                                                                              = void (**)();
        ListenerArray (*originalGetListener)(wl_proxy*)                                                                  = nullptr;
        const char* (*originalClass)(wl_proxy*)                                                                          = nullptr;

        /** @brief wl_proxy_get_listener returns registered implementations through a non-const ABI type. */
        ListenerArray listenerArray(const void* implementation) {
            return reinterpret_cast<ListenerArray>(const_cast<void*>(implementation)); // NOLINT(cppcoreguidelines-pro-type-const-cast) libwayland ABI
        }
        int (*originalFlush)(wl_display*)                                                               = nullptr;
        wl_proxy* (*originalMarshalConstructor)(wl_proxy*, uint32_t, const WlInterface*, uint32_t, ...) = nullptr;
        const WlInterface* callbackInterface                                                            = nullptr;

        void* (*newXkbContext)(int)                         = nullptr;
        void (*releaseXkbContext)(void*)                    = nullptr;
        void* (*newXkbKeymap)(void*, const char*, int, int) = nullptr;
        void (*releaseXkbKeymap)(void*)                     = nullptr;
        uint32_t (*lookupXkbModifier)(void*, const char*)   = nullptr;

        /**
         * @brief Resolves a Wayland or XKB function address.
         * @param handle Loaded shared library handle.
         * @param name Dynamic symbol name.
         * @return Typed function address.
         */
        template <typename Type>
        Type resolve(void* handle, const char* name) {
            return reinterpret_cast<Type>(lotus_apphook_native_dlsym(handle, name));
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
            Keyboard*        first  = nullptr;
            Keyboard*        active = nullptr;
            unsigned         count  = 0;
            for (const auto& keyboard : keyboards) {
                if (!keyboard->alive || !keyboard->focused || ((display != nullptr) && (keyboard->display != nullptr) && keyboard->display != display))
                    continue;
                if (first == nullptr)
                    first = keyboard.get();
                if (keyboard.get() == lastInputKeyboard)
                    active = keyboard.get();
                ++count;
            }
            // Multiple seats may own focus at once. Route to the keyboard that
            // actually received the last physical key, or fail closed if none
            // has been observed since the most recent enter.
            return count == 1 ? first : active;
        }

        // IPC focus indicates whether any keyboard has a live focused surface.
        // Selecting a unique injection target is a separate, stricter check:
        // multiple seats can legitimately be focused within one process.
        bool hasFocusedKeyboard() {
            return std::any_of(keyboards.begin(), keyboards.end(),
                               [](const auto& keyboard) { return keyboard->alive && keyboard->focused && keyboard->focusedSurface && keyboard->focusToken; });
        }

        std::shared_ptr<Keyboard> retainKeyboard(Keyboard* keyboard) {
            std::scoped_lock lock(keyboardMutex);
            const auto       it = std::find_if(keyboards.begin(), keyboards.end(), [keyboard](const auto& item) { return item.get() == keyboard; });
            return it == keyboards.end() ? nullptr : *it;
        }

        std::shared_ptr<Keyboard> retainKeyboardByProxy(wl_proxy* proxy) {
            std::scoped_lock lock(keyboardMutex);
            const auto       it = std::find_if(keyboards.begin(), keyboards.end(), [proxy](const auto& item) { return item->proxy == proxy; });
            return it == keyboards.end() ? nullptr : *it;
        }

        void invalidateWaylandFocus(wl_display* display, uintptr_t token);

        // Keep callback data alive during reentrant wl_keyboard_release/destroy.
        void forgetProxy(wl_proxy* proxy) {
            retireForward(proxy);
            bool changed   = false;
            bool available = false;
            bool focused   = false;
            {
                std::scoped_lock lock(keyboardMutex);
                for (auto it = keyboards.begin(); it != keyboards.end();) {
                    if ((*it)->proxy == proxy) {
                        invalidateWaylandFocus((*it)->display, (*it)->focusToken);
                        (*it)->alive   = false;
                        (*it)->focused = false;
                        if (lastInputKeyboard == it->get())
                            lastInputKeyboard = nullptr;
                        (*it)->focusedSurface = nullptr;
                        (*it)->focusToken     = 0;
                        it                    = keyboards.erase(it);
                        changed               = true;
                    } else {
                        ++it;
                    }
                }
                available = !keyboards.empty();
                focused   = hasFocusedKeyboard();
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
            std::scoped_lock lock(xkbMutex);
            if (xkb != nullptr)
                return;
            xkb = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
            if (xkb == nullptr)
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
            if (fd < 0 || size == 0 || size > (1U << 20U))
                return;
            loadXkb();
            if ((newXkbContext == nullptr) || (newXkbKeymap == nullptr) || (lookupXkbModifier == nullptr))
                return;
            void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapping == MAP_FAILED) {
                return;
            }
            const auto                             unmap = [size](void* address) { munmap(address, size); };
            std::unique_ptr<void, decltype(unmap)> ownedMapping(mapping, unmap);
            std::string                            keymap(static_cast<const char*>(ownedMapping.get()), size);
            void*                                  context = newXkbContext(0);
            if (context == nullptr)
                return;
            void* map = newXkbKeymap(context, keymap.c_str(), 1, 0);
            if (map != nullptr) {
                keyboard->shiftIndex = lookupXkbModifier(map, "Shift");
                keyboard->ctrlIndex  = lookupXkbModifier(map, "Control");
                keyboard->altIndex   = lookupXkbModifier(map, "Mod1");
                keyboard->superIndex = lookupXkbModifier(map, "Mod4");
                if (keyboard->forward != nullptr) {
                    const auto     index = [](uint32_t value) { return std::min(value, 255U); };
                    const uint32_t indices =
                        index(keyboard->shiftIndex) | (index(keyboard->ctrlIndex) << 8U) | (index(keyboard->altIndex) << 16U) | (index(keyboard->superIndex) << 24U);
                    keyboard->forward->modifierIndices.store(indices, std::memory_order_release);
                }
                if (releaseXkbKeymap != nullptr)
                    releaseXkbKeymap(map);
            }
            if (releaseXkbContext != nullptr)
                releaseXkbContext(context);
        }

        /**
         * @brief Forwards an event to the original listener or dispatcher.
         * @param keyboard Tracked keyboard state.
         * @param opcode Wayland keyboard event opcode.
         * @param args Wire-format callback arguments.
         */
        void deliverOriginal(const KeyboardListener* listener, Dispatcher dispatcher, const void* implementation, wl_proxy* proxy, void* data, uint32_t opcode, WlArgument* args) {
            if (dispatcher != nullptr) {
                static const std::array<WlMessage, 6> messages{
                    {
                        {.name = "keymap", .signature = "uhu", .types = nullptr},
                        {.name = "enter", .signature = "uoa", .types = nullptr},
                        {.name = "leave", .signature = "uo", .types = nullptr},
                        {.name = "key", .signature = "uuuu", .types = nullptr},
                        {.name = "modifiers", .signature = "uuuuu", .types = nullptr},
                        {.name = "repeat_info", .signature = "ii", .types = nullptr},
                    },
                };
                // Dispatcher target is the wl_proxy, not the proxy's user_data.
                if (opcode < messages.size())
                    (void)dispatcher(implementation, proxy, opcode, &messages.at(opcode), args);
                return;
            }
            if (listener == nullptr)
                return;
            auto*        keyboardProxy = reinterpret_cast<wl_keyboard*>(proxy);
            const WlArgs in(args);
            switch (opcode) {
                case 0:
                    if (listener->keymap != nullptr)
                        listener->keymap(data, keyboardProxy, in.uintAt(0), in.fdAt(1), in.uintAt(2));
                    break;
                case 1:
                    if (listener->enter != nullptr)
                        listener->enter(data, keyboardProxy, in.uintAt(0), static_cast<wl_surface*>(in.objectAt(1)), static_cast<wl_array*>(in.arrayAt(2)));
                    break;
                case 2:
                    if (listener->leave != nullptr)
                        listener->leave(data, keyboardProxy, in.uintAt(0), static_cast<wl_surface*>(in.objectAt(1)));
                    break;
                case 3:
                    if (listener->key != nullptr)
                        listener->key(data, keyboardProxy, in.uintAt(0), in.uintAt(1), in.uintAt(2), in.uintAt(3));
                    break;
                case 4:
                    if (listener->modifiers != nullptr)
                        listener->modifiers(data, keyboardProxy, in.uintAt(0), in.uintAt(1), in.uintAt(2), in.uintAt(3), in.uintAt(4));
                    break;
                case 5:
                    if (listener->repeatInfo != nullptr)
                        listener->repeatInfo(data, keyboardProxy, in.intAt(0), in.intAt(1));
                    break;
                default: break;
            }
        }

        // The proxy still owns our wrapper after fork. Adopt its immutable
        // forwarding descriptor into a fresh generation (never the old mutex or
        // Keyboard object). The first callback is sufficient to reactivate IPC.
        std::shared_ptr<Keyboard> adoptInheritedKeyboard(NativeForward* record, uint32_t opcode) {
            if ((record == nullptr) || !record->alive.load(std::memory_order_acquire) || (lotus_apphook_activate_inherited_keyboard() == 0))
                return {};
            loadWayland();
            if (!waylandCapable.load(std::memory_order_acquire))
                return {};
            std::shared_ptr<Keyboard> adopted;
            bool                      focused = false;
            {
                std::scoped_lock lock(keyboardMutex);
                for (const auto& keyboard : keyboards) {
                    if (keyboard->proxy == record->proxy && keyboard->alive)
                        return keyboard;
                }
                if (!record->alive.load(std::memory_order_acquire))
                    return {};
                adopted                 = std::make_shared<Keyboard>();
                adopted->proxy          = record->proxy;
                adopted->display        = record->display;
                adopted->listener       = record->listener;
                adopted->dispatcher     = record->dispatcher;
                adopted->implementation = record->implementation;
                adopted->forward        = record;
                // A leave/enter event will update focus itself. For all other
                // events restore only the last *committed* compositor focus.
                wl_surface* surface = opcode == 1 || opcode == 2 ? nullptr : record->surface.load(std::memory_order_acquire);
                if (surface != nullptr) {
                    adopted->focused        = true;
                    adopted->focusedSurface = surface;
                    adopted->focusToken     = nextFocusToken.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (adopted->focusToken == 0U)
                        adopted->focusToken = nextFocusToken.fetch_add(1, std::memory_order_relaxed) + 1;
                    focused = true;
                }
                adopted->serial = record->serial.load(std::memory_order_acquire);
                adopted->time   = record->time.load(std::memory_order_acquire);
                adopted->depressed.store(record->depressed.load(std::memory_order_acquire), std::memory_order_release);
                adopted->latched       = record->latched.load(std::memory_order_acquire);
                adopted->locked        = record->locked.load(std::memory_order_acquire);
                adopted->group         = record->group.load(std::memory_order_acquire);
                const uint32_t indices = record->modifierIndices.load(std::memory_order_acquire);
                adopted->shiftIndex    = indices & 0xffU;
                adopted->ctrlIndex     = (indices >> 8U) & 0xffU;
                adopted->altIndex      = (indices >> 16U) & 0xffU;
                adopted->superIndex    = (indices >> 24U) & 0xffU;
                keyboards.push_back(adopted);
            }
            noteWaylandKeyboardAvailable(true);
            if (focused) {
                runtime().waylandDisplay.store(adopted->display, std::memory_order_release);
                noteWaylandFocus(true);
            }
            return adopted;
        }

        // Keep the exact original callback path if recovery is unavailable.
        bool forwardInherited(wl_proxy* proxy, void* data, uint32_t opcode, WlArgument* args) {
            auto* record = inheritedForward(proxy);
            if (record == nullptr)
                return false;
            if (adoptInheritedKeyboard(record, opcode))
                return false;
            deliverOriginal(record->listener, record->dispatcher, record->implementation, proxy, data, opcode, args);
            return true;
        }

        void deliver(Keyboard* keyboard, uint32_t opcode, WlArgument* args) {
            if ((keyboard == nullptr) || !keyboard->alive)
                return;
            // Synthetic events read the application's current user_data.
            void* data = (originalGetUserData != nullptr) ? originalGetUserData(keyboard->proxy) : nullptr;
            deliverOriginal(keyboard->listener, keyboard->dispatcher, keyboard->implementation, keyboard->proxy, data, opcode, args);
        }

        /** Cancel queued work from a surface that has lost keyboard focus. */
        void invalidateWaylandFocus(wl_display* display, uintptr_t token) {
            if ((display == nullptr) || (token == 0U))
                return;
            std::scoped_lock lock(runtime().mutex);
            auto&            pending = runtime().pending;
            for (auto it = pending.begin(); it != pending.end();) {
                const auto& target = (*it)->target;
                if (target.backend == HookBackend::Wayland && target.connection == display && target.focused == token) {
                    (*it)->cancelled.store(true, std::memory_order_release);
                    finishLocked(*it, -EINPROGRESS);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
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
            NativeForward*   forward = keyboard->forward;
            const WlArgs     in(args);
            // Hide in-progress transitions from a child forked mid-callback.
            if ((forward != nullptr) && (opcode == 1 || opcode == 2))
                forward->surface.store(nullptr, std::memory_order_release);
            if (opcode == 1) {
                // A new focus could belong to another seat. Do not reuse the
                // previous key's destination until the next physical press.
                lastInputKeyboard = nullptr;
                invalidateWaylandFocus(keyboard->display, keyboard->focusToken);
                // wl_keyboard focus is per seat, not per wl_display. An enter
                // on another seat does not revoke this keyboard's focus.
                keyboard->focused        = true;
                keyboard->focusedSurface = static_cast<wl_surface*>(in.objectAt(1));
                // An old request must not follow the same keyboard proxy into
                // a newly entered surface, even if leave/enter were coalesced
                // before the IPC task could be delivered.
                keyboard->focusToken = nextFocusToken.fetch_add(1, std::memory_order_relaxed) + 1;
                if (keyboard->focusToken == 0)
                    keyboard->focusToken = nextFocusToken.fetch_add(1, std::memory_order_relaxed) + 1;
                keyboard->serial = in.uintAt(0);
                if (keyboard->display != nullptr)
                    runtime().waylandDisplay.store(keyboard->display, std::memory_order_release);
            } else if (opcode == 2) {
                invalidateWaylandFocus(keyboard->display, keyboard->focusToken);
                if (lastInputKeyboard == keyboard)
                    lastInputKeyboard = nullptr;
                keyboard->focused        = false;
                keyboard->focusedSurface = nullptr;
                keyboard->focusToken     = 0;
            } else if (opcode == 3) {
                // A key is not an enter: do not invent focus before the
                // actual wl_keyboard.enter has reached this listener.
                if (keyboard->focused && in.uintAt(3) == 1)
                    lastInputKeyboard = keyboard;
                keyboard->serial = in.uintAt(0);
                keyboard->time   = in.uintAt(1);
            } else if (opcode == 4) {
                keyboard->serial    = in.uintAt(0);
                keyboard->depressed = in.uintAt(1);
                keyboard->latched   = in.uintAt(2);
                keyboard->locked    = in.uintAt(3);
                keyboard->group     = in.uintAt(4);
            }
            if (forward != nullptr) {
                if (opcode == 3 || opcode == 4 || opcode == 1)
                    forward->serial.store(keyboard->serial, std::memory_order_release);
                if (opcode == 3)
                    forward->time.store(keyboard->time, std::memory_order_release);
                if (opcode == 4) {
                    forward->depressed.store(keyboard->depressed.load(std::memory_order_acquire), std::memory_order_release);
                    forward->latched.store(keyboard->latched, std::memory_order_release);
                    forward->locked.store(keyboard->locked, std::memory_order_release);
                    forward->group.store(keyboard->group, std::memory_order_release);
                }
                if (opcode == 1)
                    forward->surface.store(keyboard->focusedSurface, std::memory_order_release);
            }
            if (opcode >= 1 && opcode <= 3) {
                noteWaylandFocus(hasFocusedKeyboard());
            }
        }

        /**
         * @brief Intercepts a Wayland keymap callback and forwards it.
         * @param data Tracked keyboard user data.
         * @param format Keymap format.
         * @param fd Keymap file descriptor.
         * @param size Keymap size in bytes.
         */
        void onKeymap(void* data, wl_keyboard* proxy, uint32_t format, int32_t fd, uint32_t size) {
            std::array  argv{WlArgument{.u = format}, WlArgument{.h = fd}, WlArgument{.u = size}};
            WlArgument* args = argv.data();
            if (forwardInherited(reinterpret_cast<wl_proxy*>(proxy), data, 0, args))
                return;
            auto retained = retainKeyboardByProxy(reinterpret_cast<wl_proxy*>(proxy));
            if (!retained || !retained->alive)
                return;

            auto* keyboard = retained.get();
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
        void onEnter(void* data, wl_keyboard* proxy, uint32_t serial, wl_surface* surface, wl_array* keys) {
            std::array  argv{WlArgument{.u = serial}, WlArgument{.o = surface}, WlArgument{.a = keys}};
            WlArgument* args = argv.data();
            if (forwardInherited(reinterpret_cast<wl_proxy*>(proxy), data, 1, args))
                return;
            auto retained = retainKeyboardByProxy(reinterpret_cast<wl_proxy*>(proxy));
            if (!retained || !retained->alive)
                return;

            auto* keyboard = retained.get();
            observe(keyboard, 1, args);
            deliver(keyboard, 1, args);
        }

        /**
         * @brief Tracks keyboard focus exit and forwards the event.
         * @param data Tracked keyboard user data.
         * @param serial Compositor input serial.
         * @param surface Exited surface.
         */
        void onLeave(void* data, wl_keyboard* proxy, uint32_t serial, wl_surface* surface) {
            std::array  argv{WlArgument{.u = serial}, WlArgument{.o = surface}};
            WlArgument* args = argv.data();
            if (forwardInherited(reinterpret_cast<wl_proxy*>(proxy), data, 2, args))
                return;
            auto retained = retainKeyboardByProxy(reinterpret_cast<wl_proxy*>(proxy));
            if (!retained || !retained->alive)
                return;

            auto* keyboard = retained.get();
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
        void onKey(void* data, wl_keyboard* proxy, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
            std::array  argv{WlArgument{.u = serial}, WlArgument{.u = time}, WlArgument{.u = key}, WlArgument{.u = state}};
            WlArgument* args = argv.data();
            if (forwardInherited(reinterpret_cast<wl_proxy*>(proxy), data, 3, args))
                return;
            auto retained = retainKeyboardByProxy(reinterpret_cast<wl_proxy*>(proxy));
            if (!retained || !retained->alive)
                return;

            auto* keyboard = retained.get();
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
        void onModifiers(void* data, wl_keyboard* proxy, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
            std::array  argv{WlArgument{.u = serial}, WlArgument{.u = depressed}, WlArgument{.u = latched}, WlArgument{.u = locked}, WlArgument{.u = group}};
            WlArgument* args = argv.data();
            if (forwardInherited(reinterpret_cast<wl_proxy*>(proxy), data, 4, args))
                return;
            auto retained = retainKeyboardByProxy(reinterpret_cast<wl_proxy*>(proxy));
            if (!retained || !retained->alive)
                return;

            auto* keyboard = retained.get();
            observe(keyboard, 4, args);
            deliver(keyboard, 4, args);
        }

        /**
         * @brief Forwards keyboard key repeat settings.
         * @param data Tracked keyboard user data.
         * @param rate Repeat rate.
         * @param delay Initial repeat delay.
         */
        void onRepeat(void* data, wl_keyboard* proxy, int32_t rate, int32_t delay) {
            std::array  argv{WlArgument{.i = rate}, WlArgument{.i = delay}};
            WlArgument* args = argv.data();
            if (forwardInherited(reinterpret_cast<wl_proxy*>(proxy), data, 5, args))
                return;
            auto retained = retainKeyboardByProxy(reinterpret_cast<wl_proxy*>(proxy));
            if (retained && retained->alive)
                deliver(retained.get(), 5, args);
        }

        // libwayland's listener API takes a non-const pointer; this table is never modified.
        KeyboardListener wrapper{.keymap = onKeymap, .enter = onEnter, .leave = onLeave, .key = onKey, .modifiers = onModifiers, .repeatInfo = onRepeat};

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
            const auto* record = static_cast<const NativeForward*>(implementation);
            if ((record == nullptr) || !record->alive.load(std::memory_order_acquire))
                return 0;
            if (record->ownerPid != getpid() && !adoptInheritedKeyboard(publishedForward(record), opcode))
                return (record->dispatcher != nullptr) ? record->dispatcher(record->implementation, data, opcode, message, args) : 0;
            auto retained = record->ownerPid == getpid() ? retainKeyboard(record->tracked) : retainKeyboardByProxy(record->proxy);
            if (!retained || !retained->alive)
                return 0;
            auto*        keyboard = retained.get();
            const WlArgs in(args);
            if (opcode == 0 && (args != nullptr) && in.uintAt(0) == 1)
                readKeymap(keyboard, in.fdAt(1), in.uintAt(2));
            if (opcode >= 1 && opcode <= 4 && (args != nullptr))
                observe(keyboard, opcode, args);
            if (opcode == 3 && (args != nullptr) && in.uintAt(3) == 1 && keyboard->focused) {
                noteKeyboardActivity(true);
            }
            return (keyboard->dispatcher != nullptr) ? keyboard->dispatcher(keyboard->implementation, data, opcode, message, args) : 0;
        }

        /**
         * @brief Converts Lotus modifier bits to the target keyboard keymap mask.
         * @param keyboard Tracked keyboard state.
         * @param modifiers Lotus modifier bitmask.
         * @return Wayland depressed modifier bits.
         */
        uint32_t modifierMask(const Keyboard& keyboard, uint32_t modifiers) {
            uint32_t mask = 0;
            if (((modifiers & kModifierShift) != 0U) && keyboard.shiftIndex < 32)
                mask |= 1U << keyboard.shiftIndex;
            if (((modifiers & kModifierCtrl) != 0U) && keyboard.ctrlIndex < 32)
                mask |= 1U << keyboard.ctrlIndex;
            if (((modifiers & kModifierAlt) != 0U) && keyboard.altIndex < 32)
                mask |= 1U << keyboard.altIndex;
            if (((modifiers & kModifierSuper) != 0U) && keyboard.superIndex < 32)
                mask |= 1U << keyboard.superIndex;
            return mask;
        }

        /**
         * @brief Delivers a synthetic modifier state event.
         * @param keyboard Focused keyboard.
         * @param depressed Depressed modifier bits.
         */
        void emitModifiers(Keyboard* keyboard, uint32_t depressed) {
            std::array argv{
                WlArgument{.u = keyboard->serial}, WlArgument{.u = depressed},       WlArgument{.u = keyboard->latched},
                WlArgument{.u = keyboard->locked}, WlArgument{.u = keyboard->group},
            };
            WlArgument* args = argv.data();
            deliver(keyboard, 4, args);
        }

        /**
         * @brief Delivers one synthetic Wayland keyboard key edge.
         * @param keyboard Focused keyboard.
         * @param keycode Evdev key code.
         * @param pressed One for press, zero for release.
         */
        void emitKey(Keyboard* keyboard, uint32_t keycode, uint32_t pressed) {
            std::array  argv{WlArgument{.u = keyboard->serial}, WlArgument{.u = ++keyboard->time}, WlArgument{.u = keycode}, WlArgument{.u = pressed}};
            WlArgument* args = argv.data();
            deliver(keyboard, 3, args);
        }

        /**
         * @brief Delivers queued synthetic key taps to the focused Wayland keyboard.
         */
        void pump(wl_display* display, wl_event_queue* queue) {
            std::shared_ptr<Keyboard> keyboard;
            uintptr_t                 focusToken = 0;
            {
                std::scoped_lock lock(keyboardMutex);
                Keyboard*        focused = focusedKeyboard(display);
                if ((focused != nullptr) && focused->alive && (focused->focusToken != 0U) && (focused->focusedSurface != nullptr) && (originalGetQueue != nullptr) &&
                    originalGetQueue(focused->proxy) == queue) {
                    keyboard   = retainKeyboard(focused);
                    focusToken = focused->focusToken;
                }
            }
            if (!keyboard)
                return;
            auto task = takeTask(HookBackend::Wayland, display, focusToken);
            if (!task)
                return;
            auto sameFocus = [&] {
                std::scoped_lock lock(keyboardMutex);
                return keyboard->alive && keyboard->focused && keyboard->focusedSurface && keyboard->focusToken == focusToken;
            };
            KeySequence sequence(task->ops);
            while (const KeyOperation* op = sequence.operation()) {
                if (!sameFocus() || task->cancelled.load(std::memory_order_acquire))
                    break;
                const uint32_t base = keyboard->depressed.load(std::memory_order_acquire);
                // Synthetic operations specify their complete modifier state.
                // Preserving a physically held Shift turns Delete into Cut.
                const uint32_t combined = modifierMask(*keyboard, op->modifiers);
                if (combined != base)
                    emitModifiers(keyboard.get(), combined);
                while (sequence.operation() == op && sameFocus() && (!task->cancelled.load(std::memory_order_acquire) || sequence.needsRelease())) {
                    const auto edge = sequence.current();
                    if (!edge)
                        break;
                    if (edge->pressed)
                        task->applied.store(true, std::memory_order_release);
                    emitKey(keyboard.get(), edge->keycode, edge->pressed ? 1 : 0);
                    sequence.advance();
                }
                if (combined != base && sameFocus()) {
                    // Reentrant dispatch may have delivered real modifiers while
                    // synthetic keys were being consumed. Restore the latest
                    // compositor state, not the stale pre-burst snapshot.
                    emitModifiers(keyboard.get(), keyboard->depressed.load(std::memory_order_acquire));
                }
            }
            int status = 0;
            if (!keyboard->alive)
                status = -ENOTCONN;
            else if (!sameFocus())
                status = -EINPROGRESS;
            else if (task->cancelled.load(std::memory_order_acquire))
                status = -ECANCELED;
            std::scoped_lock lock(runtime().mutex);
            finishLocked(task, status);
        }

        /**
         * @brief Processes keys only after queued Wayland events.
         * @param display Current Wayland display.
         * @param completed Whether the original dispatch has returned successfully.
         */
        void onDispatch(wl_display* display, wl_event_queue* queue, bool completed) {
            (void)queue;
            (void)completed;
            if (display != nullptr) {
                wl_display* expected = nullptr;
                (void)runtime().waylandDisplay.compare_exchange_strong(expected, display, std::memory_order_acq_rel);
            }
        }

        /** @brief Checks for queued native events without dispatching an extra event. */
        bool queueDrained(wl_display* display, wl_event_queue* queue) {
            if ((display == nullptr) || (queue == nullptr) || (originalPrepareReadQueue == nullptr) || (originalCancelRead == nullptr))
                return false;
            if (originalPrepareReadQueue(display, queue) != 0)
                return false;
            originalCancelRead(display);
            return true;
        }

        /**
         * @brief Pumps work from the wake callback, including native single-event dispatch.
         * @param callback Completed callback proxy.
         */
        void callbackDone(void* /*unused*/, wl_proxy* callback, uint32_t /*unused*/) {
            // Do not export optional Wayland symbols just to intercept dispatch:
            // the native single-event APIs also deliver this callback.
            wl_display*     display = (originalGetProxyDisplay != nullptr) ? originalGetProxyDisplay(callback) : nullptr;
            wl_event_queue* queue   = (originalGetQueue != nullptr) ? originalGetQueue(callback) : nullptr;
            if (originalDestroy != nullptr)
                originalDestroy(callback);
            if (queueDrained(display, queue)) {
                onDispatch(display, queue, true);
                if (runtime().workersStarted.load(std::memory_order_acquire))
                    pump(display, queue);
                // A native focus change may be queued after this callback. Place
                // another wake behind it instead of injecting into stale focus.
                bool pending = false;
                {
                    std::scoped_lock lock(runtime().mutex);
                    pending = std::any_of(runtime().pending.begin(), runtime().pending.end(),
                                          [display](const auto& task) { return task->target.backend == HookBackend::Wayland && task->target.connection == display; });
                }
                if (pending)
                    wakeWayland();
            }
        }

    } // namespace

    FocusCandidates focusedWaylandCandidates() {
        if (currentWayland.load(std::memory_order_acquire) == nullptr)
            return {};
        std::scoped_lock lock(keyboardMutex);
        FocusCandidates  candidates;
        for (const auto& keyboard : keyboards) {
            if (!keyboard->alive || !keyboard->focused || (keyboard->display == nullptr))
                continue;
            if ((keyboard->focusToken == 0U) || (keyboard->focusedSurface == nullptr))
                continue;
            ++candidates.count;
            candidates.target = {.backend = HookBackend::Wayland, .connection = keyboard->display, .focused = keyboard->focusToken};
        }
        if (candidates.count > 1 && lastInputKeyboard && lastInputKeyboard->alive && lastInputKeyboard->focused && lastInputKeyboard->display &&
            lastInputKeyboard->focusedSurface && lastInputKeyboard->focusToken) {
            candidates.count  = 1;
            candidates.target = {.backend = HookBackend::Wayland, .connection = lastInputKeyboard->display, .focused = lastInputKeyboard->focusToken};
        }
        return candidates;
    }

    void loadWayland() {
        ensureRuntimeInitialized();
        std::call_once(resolveOnce, [] {
            wayland = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD);
            if (!wayland)
                return;
            originalAddListener                = resolve<decltype(originalAddListener)>(wayland, "wl_proxy_add_listener");
            originalAddDispatcher              = resolve<decltype(originalAddDispatcher)>(wayland, "wl_proxy_add_dispatcher");
            originalDispatchQueuePending       = resolve<decltype(originalDispatchQueuePending)>(wayland, "wl_display_dispatch_queue_pending");
            originalDispatchPending            = resolve<decltype(originalDispatchPending)>(wayland, "wl_display_dispatch_pending");
            originalDispatch                   = resolve<decltype(originalDispatch)>(wayland, "wl_display_dispatch");
            originalDispatchQueue              = resolve<decltype(originalDispatchQueue)>(wayland, "wl_display_dispatch_queue");
            originalDispatchTimeout            = resolve<decltype(originalDispatchTimeout)>(wayland, "wl_display_dispatch_timeout");
            originalDispatchQueueTimeout       = resolve<decltype(originalDispatchQueueTimeout)>(wayland, "wl_display_dispatch_queue_timeout");
            originalDispatchPendingSingle      = resolve<decltype(originalDispatchPendingSingle)>(wayland, "wl_display_dispatch_pending_single");
            originalDispatchQueuePendingSingle = resolve<decltype(originalDispatchQueuePendingSingle)>(wayland, "wl_display_dispatch_queue_pending_single");
            originalPrepareReadQueue           = resolve<decltype(originalPrepareReadQueue)>(wayland, "wl_display_prepare_read_queue");
            originalCancelRead                 = resolve<decltype(originalCancelRead)>(wayland, "wl_display_cancel_read");
            originalConnect                    = resolve<decltype(originalConnect)>(wayland, "wl_display_connect");
            originalConnectFd                  = resolve<decltype(originalConnectFd)>(wayland, "wl_display_connect_to_fd");
            originalGetFd                      = resolve<decltype(originalGetFd)>(wayland, "wl_display_get_fd");
            originalDestroy                    = resolve<decltype(originalDestroy)>(wayland, "wl_proxy_destroy");
            originalDisconnect                 = resolve<decltype(originalDisconnect)>(wayland, "wl_display_disconnect");
            originalGetQueue                   = resolve<decltype(originalGetQueue)>(wayland, "wl_proxy_get_queue");
            originalSetQueue                   = resolve<decltype(originalSetQueue)>(wayland, "wl_proxy_set_queue");
            originalMarshalArrayFlags          = resolve<decltype(originalMarshalArrayFlags)>(wayland, "wl_proxy_marshal_array_flags");
            originalGetProxyDisplay            = resolve<decltype(originalGetProxyDisplay)>(wayland, "wl_proxy_get_display");
            originalGetUserData                = resolve<decltype(originalGetUserData)>(wayland, "wl_proxy_get_user_data");
            originalGetListener                = resolve<decltype(originalGetListener)>(wayland, "wl_proxy_get_listener");
            originalClass                      = resolve<decltype(originalClass)>(wayland, "wl_proxy_get_class");
            originalFlush                      = resolve<decltype(originalFlush)>(wayland, "wl_display_flush");
            originalMarshalConstructor         = resolve<decltype(originalMarshalConstructor)>(wayland, "wl_proxy_marshal_constructor_versioned");
            callbackInterface                  = resolve<const WlInterface*>(wayland, "wl_callback_interface");
            // Required primitives for event ordering, per-queue injection, and wakeups.
            // The timeout dispatch APIs (Wayland 1.24+) are optional.
            const bool capable = originalAddListener && originalAddDispatcher && originalDispatchQueuePending && originalDispatchPending && originalDispatch &&
                originalDispatchQueue && originalDestroy && originalDisconnect && originalGetQueue && originalSetQueue && originalMarshalArrayFlags && originalGetProxyDisplay &&
                originalGetUserData && originalGetListener && originalClass && originalFlush && originalGetFd && originalMarshalConstructor && callbackInterface;
            waylandCapable.store(capable, std::memory_order_release);
        });
    }

    void resetWaylandAfterFork() {
        // Preserve the parent's recursive mutex and listener state untouched.
        // The child builds a fresh state if a keyboard frontend is activated.
        currentWayland.store(nullptr, std::memory_order_release);
        new (&resolveOnce) std::once_flag;
        waylandCapable.store(false, std::memory_order_release);
    }

    void wakeWayland() {
        if (currentWayland.load(std::memory_order_acquire) == nullptr)
            return;
        std::scoped_lock connectionLock(waylandConnectionMutex);
        auto&            generation = waylandGeneration();
        if (!waylandCapable.load(std::memory_order_acquire))
            return;
        // A previous wake may already be queued but not yet written because
        // the socket was full. Retry it instead of adding duplicate callbacks.
        if (generation.pendingFlushDisplay != nullptr) {
            const int flushed = originalFlush(generation.pendingFlushDisplay);
            if (flushed < 0 && errno == EAGAIN)
                return;
            generation.pendingFlushDisplay = nullptr;
            return;
        }
        wl_display*     display = nullptr;
        wl_event_queue* queue   = nullptr;
        {
            std::scoped_lock lock(keyboardMutex);
            Keyboard*        keyboard = focusedKeyboard();
            if ((keyboard == nullptr) || !keyboard->alive || (keyboard->display == nullptr))
                return;
            display = keyboard->display;
            queue   = originalGetQueue(keyboard->proxy);
        }
        if (queue == nullptr)
            return;
        wl_proxy* callback = originalMarshalConstructor(reinterpret_cast<wl_proxy*>(display), 0, callbackInterface, 1, nullptr);
        if (callback == nullptr)
            return;
        originalSetQueue(callback, queue);
        // libwayland's listener API takes a non-const pointer; this table is never modified.
        static std::array<void (*)(), 1> listener{reinterpret_cast<void (*)()>(&callbackDone)};
        if (originalAddListener(callback, listener.data(), nullptr) != 0) {
            if (originalDestroy != nullptr)
                originalDestroy(callback);
            return;
        }
        const int flushed = originalFlush(display);
        if (flushed < 0 && errno == EAGAIN) {
            generation.pendingFlushDisplay = display;
            const int notify               = runtime().completionFd.load(std::memory_order_acquire);
            if (notify >= 0) {
                const uint64_t one = 1;
                (void)retryOnEintr([&] { return write(notify, &one, sizeof(one)); });
            }
        }
    }

    int waylandWakeFlushFd() {
        if (currentWayland.load(std::memory_order_acquire) == nullptr)
            return -1;
        std::scoped_lock connectionLock(waylandConnectionMutex);
        auto&            generation = waylandGeneration();
        return (generation.pendingFlushDisplay != nullptr) && (originalGetFd != nullptr) ? originalGetFd(generation.pendingFlushDisplay) : -1;
    }

    void retryWaylandWakeFlush() {
        if (currentWayland.load(std::memory_order_acquire) == nullptr)
            return;
        std::scoped_lock connectionLock(waylandConnectionMutex);
        auto&            generation = waylandGeneration();
        if ((generation.pendingFlushDisplay == nullptr) || (originalFlush == nullptr))
            return;
        const int flushed = originalFlush(generation.pendingFlushDisplay);
        if (flushed >= 0 || errno != EAGAIN)
            generation.pendingFlushDisplay = nullptr;
    }

} // namespace lotus::apphook

// The preloaded C shim is the only externally visible interposer.
#define wl_proxy_add_listener                    lotus_apphook_impl_wl_proxy_add_listener
#define wl_proxy_add_dispatcher                  lotus_apphook_impl_wl_proxy_add_dispatcher
#define wl_proxy_get_listener                    lotus_apphook_impl_wl_proxy_get_listener
#define wl_proxy_marshal_array_flags             lotus_apphook_impl_wl_proxy_marshal_array_flags
#define wl_display_disconnect                    lotus_apphook_impl_wl_display_disconnect
#define wl_proxy_destroy                         lotus_apphook_impl_wl_proxy_destroy
#define wl_display_dispatch_queue_pending        lotus_apphook_impl_wl_display_dispatch_queue_pending
#define wl_display_dispatch_pending              lotus_apphook_impl_wl_display_dispatch_pending
#define wl_display_dispatch                      lotus_apphook_impl_wl_display_dispatch
#define wl_display_dispatch_queue                lotus_apphook_impl_wl_display_dispatch_queue
#define wl_display_dispatch_timeout              lotus_apphook_impl_wl_display_dispatch_timeout
#define wl_display_dispatch_queue_timeout        lotus_apphook_impl_wl_display_dispatch_queue_timeout
#define wl_display_dispatch_pending_single       lotus_apphook_impl_wl_display_dispatch_pending_single
#define wl_display_dispatch_queue_pending_single lotus_apphook_impl_wl_display_dispatch_queue_pending_single
#define wl_display_connect                       lotus_apphook_impl_wl_display_connect
#define wl_display_connect_to_fd                 lotus_apphook_impl_wl_display_connect_to_fd
#define LH_EXPORT                                extern "C" __attribute__((visibility("hidden")))

/**
 * @brief Intercepts listener registration and wraps newly registered keyboards.
 * @param proxy Target Wayland proxy.
 * @param callbacks Listener callback table.
 * @param data Original listener user data.
 * @return Original registration status.
 */
LH_EXPORT int wl_proxy_add_listener(wl_proxy* proxy, void (**callbacks)(), void* data) {
    using namespace lotus::apphook;
    loadWayland();
    if (originalAddListener == nullptr)
        return -1;
    if (!waylandCapable.load(std::memory_order_acquire))
        return originalAddListener(proxy, callbacks, data);
    if ((proxy == nullptr) || (callbacks == nullptr) || (originalClass == nullptr) || std::strcmp(originalClass(proxy), "wl_keyboard") != 0 || (lookupKeyboard(proxy) != nullptr))
        return originalAddListener(proxy, callbacks, data);

    std::scoped_lock lock(keyboardMutex);
    auto             keyboard = std::make_shared<Keyboard>();
    keyboard->proxy           = proxy;
    keyboard->display         = (originalGetProxyDisplay != nullptr) ? originalGetProxyDisplay(proxy) : nullptr;
    keyboard->listener        = reinterpret_cast<const KeyboardListener*>(callbacks);
    auto* record              = allocateForward();
    if (record == nullptr)
        return originalAddListener(proxy, callbacks, data);
    record->proxy     = proxy;
    record->display   = keyboard->display;
    record->listener  = keyboard->listener;
    record->tracked   = keyboard.get();
    record->ownerPid  = getpid();
    keyboard->forward = record;
    keyboards.push_back(std::move(keyboard));
    publishForward(record);
    const int result = originalAddListener(proxy, reinterpret_cast<void (**)()>(&wrapper), data);
    if (result != 0) {
        record->alive.store(false, std::memory_order_release);
        keyboards.pop_back();
    } else {
        noteWaylandKeyboardAvailable(true);
    }
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
    loadWayland();
    if (originalAddDispatcher == nullptr)
        return -1;
    if (!waylandCapable.load(std::memory_order_acquire))
        return originalAddDispatcher(proxy, dispatcher, implementation, data);
    if ((proxy == nullptr) || (dispatcher == nullptr) || (originalClass == nullptr) || std::strcmp(originalClass(proxy), "wl_keyboard") != 0 || (lookupKeyboard(proxy) != nullptr))
        return originalAddDispatcher(proxy, dispatcher, implementation, data);

    std::scoped_lock lock(keyboardMutex);
    auto             keyboard = std::make_shared<Keyboard>();
    keyboard->proxy           = proxy;
    keyboard->display         = (originalGetProxyDisplay != nullptr) ? originalGetProxyDisplay(proxy) : nullptr;
    keyboard->dispatcher      = dispatcher;
    keyboard->implementation  = implementation;
    auto* record              = allocateForward();
    if (record == nullptr)
        return originalAddDispatcher(proxy, dispatcher, implementation, data);
    record->proxy          = proxy;
    record->display        = keyboard->display;
    record->dispatcher     = dispatcher;
    record->implementation = implementation;
    record->tracked        = keyboard.get();
    record->ownerPid       = getpid();
    keyboard->forward      = record;
    keyboards.push_back(std::move(keyboard));
    publishForward(record);
    const int result = originalAddDispatcher(proxy, &dispatchWrapper, record, data);
    if (result != 0) {
        record->alive.store(false, std::memory_order_release);
        keyboards.pop_back();
    } else {
        noteWaylandKeyboardAvailable(true);
    }
    return result;
}

/** Expose the application's original listener/dispatcher implementation. */
LH_EXPORT void (**wl_proxy_get_listener(wl_proxy* proxy))() {
    using namespace lotus::apphook;
    loadWayland();
    if (originalGetListener == nullptr)
        return nullptr;
    auto keyboard = retainKeyboardByProxy(proxy);
    if (keyboard) {
        const void* implementation = (keyboard->dispatcher != nullptr) ? keyboard->implementation : static_cast<const void*>(keyboard->listener);
        return listenerArray(implementation);
    }
    if (auto* inherited = inheritedForward(proxy)) {
        const void* implementation = (inherited->dispatcher != nullptr) ? inherited->implementation : static_cast<const void*>(inherited->listener);
        return listenerArray(implementation);
    }
    return originalGetListener(proxy);
}

/** Array-based marshals use the same destruction flag. */
LH_EXPORT wl_proxy* wl_proxy_marshal_array_flags(wl_proxy* proxy, uint32_t opcode, const WlInterface* interface, uint32_t version, uint32_t flags, WlArgument* args) {
    using namespace lotus::apphook;
    loadWayland();
    if (originalMarshalArrayFlags == nullptr)
        return nullptr;
    wl_proxy* result = originalMarshalArrayFlags(proxy, opcode, interface, version, flags, args);
    if ((flags & 1U) != 0U)
        forgetProxy(proxy);
    return result;
}

/** Disconnect invalidates every keyboard and pending wakeup on this display. */
LH_EXPORT void wl_display_disconnect(wl_display* display) {
    using namespace lotus::apphook;
    loadWayland();
    retireDisplay(display);
    std::scoped_lock connectionLock(waylandConnectionMutex);
    auto&            generation = waylandGeneration();
    if (generation.pendingFlushDisplay == display)
        generation.pendingFlushDisplay = nullptr;
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
    if (originalDisconnect != nullptr)
        originalDisconnect(display);
}

/**
 * @brief Drops keyboard tracking when its Wayland proxy is destroyed.
 * @param proxy Proxy being destroyed.
 */
LH_EXPORT void wl_proxy_destroy(wl_proxy* proxy) {
    using namespace lotus::apphook;
    forgetProxy(proxy);
    loadWayland();
    if (originalDestroy != nullptr)
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
    loadWayland();
    onDispatch(display, queue, false);
    const int result = (originalDispatchQueuePending != nullptr) ? originalDispatchQueuePending(display, queue) : -1;
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
    loadWayland();
    onDispatch(display, (originalGetQueue != nullptr) ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, false);
    const int result = (originalDispatchPending != nullptr) ? originalDispatchPending(display) : -1;
    if (result >= 0)
        onDispatch(display, (originalGetQueue != nullptr) ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, true);
    return result;
}

/**
 * @brief Processes synthetic keyboard events around blocking Wayland dispatch.
 * @param display Application Wayland display.
 * @return Original dispatch status.
 */
LH_EXPORT int wl_display_dispatch(wl_display* display) {
    using namespace lotus::apphook;
    loadWayland();
    onDispatch(display, (originalGetQueue != nullptr) ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, false);
    const int result = (originalDispatch != nullptr) ? originalDispatch(display) : -1;
    if (result >= 0)
        onDispatch(display, (originalGetQueue != nullptr) ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr, true);
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
    loadWayland();
    onDispatch(display, queue, false);
    const int result = (originalDispatchQueue != nullptr) ? originalDispatchQueue(display, queue) : -1;
    if (result >= 0)
        onDispatch(display, queue, true);
    return result;
}

/**
 * @brief Intercepts timeout-limited dispatch of the default Wayland queue (1.24+).
 * @param display Application Wayland display.
 * @param timeout Maximum wait duration.
 * @return Original dispatch result, or -1 with ENOSYS if unavailable.
 */
LH_EXPORT int wl_display_dispatch_timeout(wl_display* display, const timespec* timeout) {
    using namespace lotus::apphook;
    loadWayland();
    if (originalDispatchTimeout == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    wl_event_queue* queue = (originalGetQueue != nullptr) ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr;
    onDispatch(display, queue, false);
    const int result = originalDispatchTimeout(display, timeout);
    if (result >= 0)
        onDispatch(display, queue, true);
    return result;
}

/**
 * @brief Intercepts timeout-limited dispatch of a custom Wayland queue (1.24+).
 * @param display Application Wayland display.
 * @param queue Queue being dispatched.
 * @param timeout Maximum wait duration.
 * @return Original dispatch result, or -1 with ENOSYS if unavailable.
 */
LH_EXPORT int wl_display_dispatch_queue_timeout(wl_display* display, wl_event_queue* queue, const timespec* timeout) {
    using namespace lotus::apphook;
    loadWayland();
    if (originalDispatchQueueTimeout == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    onDispatch(display, queue, false);
    const int result = originalDispatchQueueTimeout(display, queue, timeout);
    if (result >= 0)
        onDispatch(display, queue, true);
    return result;
}

/**
 * @brief Intercepts dispatch of at most one event from the default Wayland queue (1.25+).
 * @param display Application Wayland display.
 * @return Original dispatch result, or -1 with ENOSYS if unavailable.
 */
LH_EXPORT int wl_display_dispatch_pending_single(wl_display* display) {
    using namespace lotus::apphook;
    loadWayland();
    if (originalDispatchPendingSingle == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    wl_event_queue* queue = (originalGetQueue != nullptr) ? originalGetQueue(reinterpret_cast<wl_proxy*>(display)) : nullptr;
    onDispatch(display, queue, false);
    const int result = originalDispatchPendingSingle(display);
    // Returning 1 may leave native FocusOut pending. Probe the queue without
    // dispatching another event; always balance a successful prepare with cancel.
    if (result == 0 || (result > 0 && queueDrained(display, queue)))
        onDispatch(display, queue, true);
    return result;
}

/**
 * @brief Intercepts dispatch of at most one event from a custom Wayland queue (1.25+).
 * @param display Application Wayland display.
 * @param queue Queue being dispatched.
 * @return Original dispatch result, or -1 with ENOSYS if unavailable.
 */
LH_EXPORT int wl_display_dispatch_queue_pending_single(wl_display* display, wl_event_queue* queue) {
    using namespace lotus::apphook;
    loadWayland();
    if (originalDispatchQueuePendingSingle == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    onDispatch(display, queue, false);
    const int result = originalDispatchQueuePendingSingle(display, queue);
    if (result == 0 || (result > 0 && queueDrained(display, queue)))
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
    loadWayland();
    wl_display* display = (originalConnect != nullptr) ? originalConnect(name) : nullptr;
    if (display != nullptr)
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
    loadWayland();
    wl_display* display = (originalConnectFd != nullptr) ? originalConnectFd(fd) : nullptr;
    if (display != nullptr)
        runtime().waylandDisplay.store(display);
    return display;
}

// Forked children still have wrappers installed in inherited Wayland proxies.
// These lookups only touch immutable callback descriptors (no runtime locks).
extern "C" __attribute__((visibility("hidden"))) int lotus_apphook_inherited_wayland_listener(wl_proxy* proxy, void (***out)()) {
    auto* record = lotus::apphook::inheritedForward(proxy);
    if (record == nullptr)
        return 0;
    const void* implementation = (record->dispatcher != nullptr) ? record->implementation : static_cast<const void*>(record->listener);
    *out                       = lotus::apphook::listenerArray(implementation);
    return 1;
}
extern "C" __attribute__((visibility("hidden"))) void lotus_apphook_retire_wayland_proxy(wl_proxy* proxy) {
    lotus::apphook::retireForward(proxy);
}
extern "C" __attribute__((visibility("hidden"))) void lotus_apphook_retire_wayland_display(wl_display* display) {
    lotus::apphook::retireDisplay(display);
}
