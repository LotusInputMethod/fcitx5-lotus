/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: MIT
 *
 */

/**
 * @file lotus-apphook-x11.cpp
 * @brief Intercepts Xlib keyboard event delivery and injects application-local events.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-target.h"
#include "lotus-apphook-fd.h"
#include "lotus-apphook-sequence.h"
#include "lotus-apphook-xfocus.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <algorithm>
#include <array>
#include <memory>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <linux/input-event-codes.h>
#include <mutex>
#include <new>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <poll.h>
#include <sys/eventfd.h>
#include <vector>
#include <unordered_map>
#include <unistd.h>

namespace lotus::apphook {
    namespace {

        constexpr unsigned long    kInjectedSerial = 0x4c485831UL;
        constexpr std::string_view kWakePayload    = "lotus-apphook-wakeup";
        static_assert(kWakePayload.size() == sizeof(xcb_client_message_data_t::data8));

        bool isWakeEvent(const XEvent* event) {
            return event->type == ClientMessage && event->xclient.send_event != False && event->xclient.message_type == None && event->xclient.format == 8 &&
                std::memcmp(&event->xclient.data, kWakePayload.data(), kWakePayload.size()) == 0;
        }

        /** @brief Releases a reply owned through malloc(), as libxcb replies are. */
        void freeReply(void* reply) {
            std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory) libxcb ABI
        }

        /** @brief Tests whether an Xlib event mask (a signed long) selects key events. */
        constexpr bool selectsKeys(long mask) {
            constexpr unsigned long keyEvents = static_cast<unsigned long>(KeyPressMask) | static_cast<unsigned long>(KeyReleaseMask);
            return (static_cast<unsigned long>(mask) & keyEvents) != 0U;
        }

        /** @brief Reports whether an event is a key event synthesized by this hook. */
        bool isInjectedKey(const XEvent* event) {
            return (event->type == KeyPress || event->type == KeyRelease) && event->xkey.serial == kInjectedSerial;
        }

        /** @brief Reports whether an event is a key press synthesized by this hook. */
        bool isInjectedPress(const XEvent* event) {
            return isInjectedKey(event) && event->type == KeyPress;
        }

        bool sameKeycode(const XEvent* first, const XEvent* second) {
            return first->xkey.keycode == second->xkey.keycode;
        }

        /** @brief Reports whether an event is a focus transition. */
        bool isFocusChange(const XEvent* event) {
            return event->type == FocusIn || event->type == FocusOut;
        }

        struct DisplayFocus : XFocusState<Window, None> {
            std::unordered_map<Window, long> keyboardMasks;
            bool                             sawKeyboard       = false;
            Window                           sawKeyboardWindow = None;
            [[nodiscard]] bool               ready() const {
                return sawKeyboard || std::ranges::any_of(keyboardMasks, [](const auto& entry) { return selectsKeys(entry.second); });
            }
        };
        // Every blocking Xlib waiter owns its own notification descriptor.
        // Broadcasting is safe even across Displays.
        struct X11Waiter {
            int fd                                 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            X11Waiter()                            = default;
            X11Waiter(const X11Waiter&)            = delete;
            X11Waiter& operator=(const X11Waiter&) = delete;
            X11Waiter(X11Waiter&&)                 = delete;
            X11Waiter& operator=(X11Waiter&&)      = delete;
            ~X11Waiter() {
                if (fd >= 0)
                    close(fd);
            }
        };
        using WaiterList      = std::vector<std::weak_ptr<X11Waiter>>;
        using DisplayFocusMap = std::unordered_map<Display*, DisplayFocus>;
        using DisplaySet      = std::unordered_set<Display*>;
        using PressedEventMap = std::unordered_map<Display*, XEvent>;
        struct X11Generation {
            std::mutex      focusMutex;
            std::mutex      wakeWaitersMutex;
            WaiterList      wakeWaiters;
            DisplayFocusMap focusByDisplay;
            DisplaySet      xcbOwnerDisplays;
            PressedEventMap pressedEvents;
        };
        std::atomic<X11Generation*> currentX11{nullptr};
        X11Generation&              x11Generation() {
            auto* current = currentX11.load(std::memory_order_acquire);
            if (current != nullptr)
                return *current;
            auto           fresh    = std::make_unique<X11Generation>();
            X11Generation* expected = nullptr;
            if (!currentX11.compare_exchange_strong(expected, fresh.get(), std::memory_order_acq_rel))
                return *expected;
            // Generations are replaced, never freed, after fork (see resetX11AfterFork).
            return *fresh.release();
        }
#define focusMutex       (x11Generation().focusMutex)
#define wakeWaitersMutex (x11Generation().wakeWaitersMutex)
#define focusByDisplay   (x11Generation().focusByDisplay)
#define xcbOwnerDisplays (x11Generation().xcbOwnerDisplays)
#define pressedEvents    (x11Generation().pressedEvents)
        WaiterList& wakeWaitersStorage() {
            return x11Generation().wakeWaiters;
        }
        void* xlib                                        = nullptr;
        void* xlibXcb                                     = nullptr;
        void* (*getXcbConnection)(Display*)               = nullptr;
        void (*originalSetEventQueueOwner)(Display*, int) = nullptr;
        std::once_flag resolveOnce;

        int (*originalSelectInput)(Display*, Window, long)                                            = nullptr;
        int (*originalChangeWindowAttributes)(Display*, Window, unsigned long, XSetWindowAttributes*) = nullptr;
        Window (*originalCreateWindow)(Display*, Window, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, Visual*, unsigned long,
                                       XSetWindowAttributes*)                                         = nullptr;
        int (*originalNext)(Display*, XEvent*)                                                        = nullptr;
        int (*originalPeek)(Display*, XEvent*)                                                        = nullptr;
        int (*originalMask)(Display*, long, XEvent*)                                                  = nullptr;
        int (*originalWindow)(Display*, Window, long, XEvent*)                                        = nullptr;
        Bool (*originalCheckMask)(Display*, long, XEvent*)                                            = nullptr;
        Bool (*originalCheckWindow)(Display*, Window, long, XEvent*)                                  = nullptr;
        Bool (*originalCheckTyped)(Display*, int, XEvent*)                                            = nullptr;
        Bool (*originalCheckTypedWindow)(Display*, Window, int, XEvent*)                              = nullptr;
        int (*originalIf)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer)         = nullptr;
        int (*originalPeekIf)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer)     = nullptr;
        Bool (*originalCheckIf)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer)   = nullptr;
        int (*originalPending)(Display*)                                                              = nullptr;
        int (*originalPutBack)(Display*, XEvent*)                                                     = nullptr;
        void (*originalLockDisplay)(Display*)                                                         = nullptr;
        void (*originalUnlockDisplay)(Display*)                                                       = nullptr;
        int (*originalCloseDisplay)(Display*)                                                         = nullptr;
        int (*originalDestroyWindow)(Display*, Window)                                                = nullptr;
        int (*originalConnectionNumber)(Display*)                                                     = nullptr;
        Status (*originalGetFocus)(Display*, Window*, int*)                                           = nullptr;
        KeyCode (*originalKeysymToKeycode)(Display*, KeySym)                                          = nullptr;
        Window (*originalRootWindow)(Display*)                                                        = nullptr;

        const char* (*originalDisplayString)(Display*)                                                                            = nullptr;
        xcb_connection_t* (*wakeConnect)(const char*, int*)                                                                       = nullptr;
        void (*wakeDisconnect)(xcb_connection_t*)                                                                                 = nullptr;
        xcb_void_cookie_t (*wakeSendEvent)(xcb_connection_t*, uint8_t, xcb_window_t, uint32_t, const char*)                       = nullptr;
        int (*wakeFlush)(xcb_connection_t*)                                                                                       = nullptr;
        xcb_get_input_focus_cookie_t (*wakeGetFocus)(xcb_connection_t*)                                                           = nullptr;
        xcb_get_input_focus_reply_t* (*wakeGetFocusReply)(xcb_connection_t*, xcb_get_input_focus_cookie_t, xcb_generic_error_t**) = nullptr;

        /**
         * @brief Looks up an original Xlib function.
         * @param name Xlib dynamic symbol name.
         * @return Typed original Xlib function address.
         */
        template <typename Function>
        Function resolve(const char* name) {
            return reinterpret_cast<Function>(dlsym(xlib, name));
        }

        /** @brief Matches private wake events without consuming application events. */
        Bool wakeEvent(Display* /*unused*/, XEvent* event, XPointer /*unused*/) {
            return isWakeEvent(event) ? True : False;
        }
        Bool syntheticPress(Display* /*unused*/, XEvent* event, XPointer /*unused*/) {
            return isInjectedPress(event) ? True : False;
        }

        /** @brief Recomputes readiness across eligible Xlib displays. Called with focusMutex. */
        bool xlibReady() {
            return std::ranges::any_of(focusByDisplay, [](const auto& entry) { return !xcbOwnerDisplays.contains(entry.first) && entry.second.ready(); });
        }

        void updateKeyboardMask(Display* display, Window window, long mask) {
            bool ready = false;
            {
                std::scoped_lock lock(focusMutex);
                auto&            state = focusByDisplay[display];
                if (selectsKeys(mask)) {
                    state.keyboardMasks[window] = mask;
                } else {
                    state.keyboardMasks.erase(window);
                    if (state.sawKeyboardWindow == window) {
                        state.sawKeyboard       = false;
                        state.sawKeyboardWindow = None;
                    }
                }
                ready = xlibReady();
            }
            noteX11KeyboardAvailable(ready);
        }

        void forgetWindow(Display* display, Window window) {
            bool focused         = false;
            bool ready           = false;
            bool targetDestroyed = false;
            {
                std::scoped_lock lock(focusMutex);
                auto             it = focusByDisplay.find(display);
                if (it != focusByDisplay.end()) {
                    it->second.keyboardMasks.erase(window);
                    if (it->second.sawKeyboardWindow == window) {
                        it->second.sawKeyboard       = false;
                        it->second.sawKeyboardWindow = None;
                    }
                    if (it->second.target == window) {
                        targetDestroyed   = true;
                        it->second.target = None;
                    }
                    if (it->second.focus == window)
                        it->second.leave(window);
                }
                for (const auto& [current, state] : focusByDisplay)
                    if (!xcbOwnerDisplays.contains(current))
                        focused |= state.focused;
                ready = xlibReady();
            }
            {
                std::scoped_lock lock(runtime().mutex);
                if (targetDestroyed && runtime().activeX11 && runtime().activeX11Display == display)
                    runtime().activeX11->cancelled.store(true, std::memory_order_release);
            }
            noteX11KeyboardAvailable(ready);
            noteX11Focus(focused);
        }

        /** @brief Cancels undelivered edges, preserving release of a delivered press. */
        void purgeCancelled(Display* target) {
            if ((originalLockDisplay == nullptr) || (originalUnlockDisplay == nullptr))
                return;
            originalLockDisplay(target);
            Runtime& state = runtime();
            {
                std::scoped_lock lock(state.mutex);
                const auto       task = state.activeX11;
                if (task && state.activeX11Display == target && task->cancelled.load(std::memory_order_acquire)) {
                    XEvent discarded{};
                    // Keep all queued releases: filtered consumers can have
                    // received more than one press without consuming releases.
                    if (originalCheckIf != nullptr) {
                        while (originalCheckIf(target, &discarded, &syntheticPress, nullptr) != 0) {}
                    }
                    pressedEvents.erase(target);
                    finishLocked(task, -EINPROGRESS);
                    state.activeX11.reset();
                    state.activeX11Display = nullptr;
                }
            }
            originalUnlockDisplay(target);
        }

        /** @brief Moves a queued focus transition ahead of an active synthetic burst. */
        void promoteFocusTransition(Display* target) {
            if ((originalLockDisplay == nullptr) || (originalUnlockDisplay == nullptr) || (originalNext == nullptr) || (originalPending == nullptr) || (originalPutBack == nullptr))
                return;
            {
                std::scoped_lock lock(runtime().mutex);
                if (!runtime().activeX11 || runtime().activeX11Display != target)
                    return;
            }
            originalLockDisplay(target);
            const int count = originalPending(target);
            if (count > 0) {
                std::vector<XEvent> queued;
                try {
                    queued.reserve(static_cast<size_t>(count));
                } catch (const std::bad_alloc&) {
                    originalUnlockDisplay(target);
                    return;
                }
                for (int i = 0; i < count; ++i) {
                    XEvent event{};
                    originalNext(target, &event);
                    queued.push_back(event);
                }
                const bool transition = std::ranges::any_of(queued, [](const XEvent& event) { return isFocusChange(&event); });
                if (transition) {
                    // Filtered Xlib reads may skip FocusOut even when it is at
                    // the head of the queue. Cancel the remaining synthetic
                    // presses before any filtered lookup can consume them.
                    std::scoped_lock lock(runtime().mutex);
                    if (runtime().activeX11 && runtime().activeX11Display == target) {
                        runtime().activeX11->cancelled.store(true, std::memory_order_release);
                        finishLocked(runtime().activeX11, -EINPROGRESS);
                        runtime().activeX11.reset();
                        runtime().activeX11Display = nullptr;
                        pressedEvents.erase(target);
                    }
                }
                for (auto& event : std::views::reverse(queued)) {
                    if (!transition || !isInjectedPress(&event))
                        (void)originalPutBack(target, &event);
                }
            }
            originalUnlockDisplay(target);
        }

        /**
         * @brief Accounts for delivered synthetic events and observed input windows.
         * @param event Event delivered to the application.
         * @param consumed Whether the application removed the event from its queue.
         */
        void recordEvent(const XEvent& event, bool consumed) {
            if (!consumed) {
                return;
            }
            if (event.type == FocusIn || event.type == FocusOut) {
                bool focused = false;
                bool cancel  = false;
                {
                    std::scoped_lock lock(focusMutex);
                    auto&            tracked       = focusByDisplay[event.xfocus.display];
                    const bool       wasFocused    = tracked.focused;
                    const Window     previousOwner = tracked.focus;
                    if (event.type == FocusIn)
                        tracked.enter(event.xfocus.window);
                    else
                        tracked.leave(event.xfocus.window, true);
                    cancel = (wasFocused && !tracked.focused) || (event.type == FocusIn && wasFocused && previousOwner != event.xfocus.window);
                    for (const auto& [ignored, state] : focusByDisplay)
                        focused |= state.focused;
                }
                if (cancel) {
                    {
                        std::scoped_lock lock(runtime().mutex);
                        if (runtime().activeX11 && runtime().activeX11Display == event.xfocus.display)
                            runtime().activeX11->cancelled.store(true, std::memory_order_release);
                    }
                    cancelPendingTasks(HookBackend::X11, event.xfocus.display);
                }
                runtime().observedX11Focus.store(true, std::memory_order_release);
                noteX11Focus(focused);
                if (cancel)
                    purgeCancelled(event.xfocus.display);
            }
            if (event.type == KeyPress && event.xkey.serial == kInjectedSerial) {
                Runtime&         state = runtime();
                std::scoped_lock lock(state.mutex);
                if (state.activeX11 && state.activeX11Display == event.xkey.display && state.activeX11->x11Remaining > 0) {
                    auto task = state.activeX11;
                    task->applied.store(true, std::memory_order_release);
                    pressedEvents[event.xkey.display] = event;
                    // A filtered reader may never request KeyRelease. Account
                    // for delivered presses while leaving releases queued.
                    if (--task->x11Remaining == 0) {
                        finishLocked(task, task->cancelled.load(std::memory_order_acquire) ? -EINPROGRESS : 0);
                        state.activeX11.reset();
                        state.activeX11Display = nullptr;
                        pressedEvents.erase(event.xkey.display);
                    }
                }
            } else if (event.type == KeyRelease && event.xkey.serial == kInjectedSerial) {
                std::scoped_lock lock(runtime().mutex);
                auto             it = pressedEvents.find(event.xkey.display);
                if (it != pressedEvents.end() && sameKeycode(&it->second, &event))
                    pressedEvents.erase(it);
            }
            if ((event.type == KeyPress || event.type == KeyRelease) && event.xkey.serial != kInjectedSerial) {
                // Real keyboard events identify the window that handles keys.
                // Other X11 event classes must never change the injection target.
                {
                    std::scoped_lock lock(focusMutex);
                    auto&            tracked = focusByDisplay[event.xkey.display];
                    tracked.keyTarget(event.xkey.window, false);
                    tracked.sawKeyboard       = true;
                    tracked.sawKeyboardWindow = event.xkey.window;
                    if (event.type == KeyPress && !tracked.observedFocus)
                        tracked.focused = true;
                }
                noteX11KeyboardAvailable(true);
                if (event.type == KeyPress)
                    noteKeyboardActivity(false);
            }
        }

        /**
         * @brief Maps an evdev key code to an Xlib keycode.
         * @param target Application Xlib display.
         * @param evdev Linux evdev key code.
         * @return Xlib keycode.
         */
        KeyCode keycode(Display* target, uint32_t evdev) {
            KeySym symbol = NoSymbol;
            switch (evdev) {
                case KEY_BACKSPACE: symbol = XK_BackSpace; break;
                case KEY_LEFT: symbol = XK_Left; break;
                case KEY_RIGHT: symbol = XK_Right; break;
                case KEY_DELETE: symbol = XK_Delete; break;
                case KEY_LEFTSHIFT: symbol = XK_Shift_L; break;
                case KEY_LEFTCTRL: symbol = XK_Control_L; break;
                default: break;
            }
            if (symbol != NoSymbol && (originalKeysymToKeycode != nullptr)) {
                const KeyCode resolved = originalKeysymToKeycode(target, symbol);
                if (resolved != 0U) {
                    return resolved;
                }
            }
            return evdev <= 247 ? static_cast<KeyCode>(evdev + 8) : 0;
        }

        /**
         * @brief Converts Lotus modifier bits into X11 modifier masks.
         * @param bits Lotus modifier bits.
         * @return X11 modifier state.
         */
        unsigned modifiers(uint32_t bits) {
            unsigned state = 0;
            if ((bits & kModifierShift) != 0U)
                state |= static_cast<unsigned>(ShiftMask);
            if ((bits & kModifierCtrl) != 0U)
                state |= static_cast<unsigned>(ControlMask);
            if ((bits & kModifierAlt) != 0U)
                state |= static_cast<unsigned>(Mod1Mask);
            if ((bits & kModifierSuper) != 0U)
                state |= static_cast<unsigned>(Mod4Mask);
            return state;
        }

        /**
         * @brief Appends synthetic keyboard events after already-queued native events.
         * @param target Application Xlib display.
         */
        void pump(Display* target) {
            if ((target == nullptr) || (originalPutBack == nullptr) || (originalNext == nullptr) || (originalPending == nullptr) || (originalLockDisplay == nullptr) ||
                (originalUnlockDisplay == nullptr))
                return;
            {
                std::scoped_lock lock(runtime().mutex);
                if (runtime().activeX11 || runtime().pending.empty())
                    return;
            }
            Window window = None;
            {
                std::scoped_lock lock(focusMutex);
                const auto       it = focusByDisplay.find(target);
                if (it != focusByDisplay.end()) {
                    if (it->second.observedFocus && !it->second.focused)
                        return;
                    window = it->second.target;
                }
            }
            if (window == None) {
                Window focused = None;
                int    revert  = 0;
                if ((originalGetFocus == nullptr) || (originalGetFocus(target, &focused, &revert) == 0) || focused == None || focused == PointerRoot)
                    return;
                window = focused;
                std::scoped_lock lock(focusMutex);
                focusByDisplay[target].target = focused;
            }
            // Keyboard subscriptions do not imply FocusChangeMask. A window
            // can remain cached as focused after another application takes
            // focus, without ever delivering FocusOut to this client.
            Window serverFocus = None;
            int    revert      = 0;
            if ((originalGetFocus == nullptr) || (originalGetFocus(target, &serverFocus, &revert) == 0) || serverFocus != window) {
                if (auto task = takeTask(HookBackend::X11, target, static_cast<uintptr_t>(window))) {
                    std::scoped_lock lock(runtime().mutex);
                    finishLocked(task, -EINPROGRESS);
                }
                return;
            }
            {
                std::scoped_lock lock(focusMutex);
                focusByDisplay[target].sawKeyboard = true;
            }
            noteX11KeyboardAvailable(true);

            // Keep the native queue order intact even when an XCheck* filter
            // ignores an unrelated event at its head. Serialise Xlib calls so
            // other application threads cannot observe the temporary drain.
            originalLockDisplay(target);
            const int queued = originalPending(target);
            if (queued < 0) {
                originalUnlockDisplay(target);
                return;
            }
            std::vector<XEvent> native;
            std::vector<XEvent> injected;
            try {
                native.reserve(static_cast<size_t>(queued));
                for (int i = 0; i < queued; ++i) {
                    XEvent event{};
                    originalNext(target, &event);
                    native.push_back(event);
                }

                // A pending focus transition must be delivered to the app first.
                // In particular, a filtered KeyPress lookup must not bypass FocusOut.
                const bool focusTransition = std::ranges::any_of(native, [](const XEvent& event) { return isFocusChange(&event); });
                if (!focusTransition) {
                    auto task = takeTask(HookBackend::X11, target, static_cast<uintptr_t>(window));
                    if (task) {
                        {
                            std::scoped_lock lock(runtime().mutex);
                            runtime().activeX11        = task;
                            runtime().activeX11Display = target;
                        }
                        size_t taps = 0;
                        for (const auto& op : task->ops)
                            taps += op.count;
                        injected.reserve(taps * 2);
                        KeySequence         sequence(task->ops);
                        const KeyOperation* lastOp     = nullptr;
                        KeyCode             code       = 0;
                        bool                invalidKey = false;
                        while (const auto edge = sequence.current()) {
                            const auto* op = sequence.operation();
                            if (op != lastOp) {
                                code   = keycode(target, op->keycode);
                                lastOp = op;
                                if (code == 0U) {
                                    invalidKey = true;
                                    break;
                                }
                            }
                            XEvent event{};
                            event.xkey.type        = edge->pressed ? KeyPress : KeyRelease;
                            event.xkey.display     = target;
                            event.xkey.window      = window;
                            event.xkey.root        = (originalRootWindow != nullptr) ? originalRootWindow(target) : window;
                            event.xkey.time        = CurrentTime;
                            event.xkey.keycode     = code;
                            event.xkey.state       = modifiers(edge->modifiers);
                            event.xkey.same_screen = True;
                            event.xkey.send_event  = True;
                            event.xkey.serial      = kInjectedSerial;
                            injected.push_back(event);
                            sequence.advance();
                        }
                        if (invalidKey || task->cancelled.load(std::memory_order_acquire)) {
                            std::scoped_lock lock(runtime().mutex);
                            finishLocked(task, invalidKey ? -EINVAL : -EINPROGRESS);
                            runtime().activeX11.reset();
                            runtime().activeX11Display = nullptr;
                            injected.clear();
                        } else {
                            std::scoped_lock lock(runtime().mutex);
                            task->x11Remaining = taps;
                        }
                    }
                }
            } catch (const std::bad_alloc&) {
                std::scoped_lock lock(runtime().mutex);
                if (runtime().activeX11) {
                    finishLocked(runtime().activeX11, -ENOMEM);
                    runtime().activeX11.reset();
                    runtime().activeX11Display = nullptr;
                }
                injected.clear();
            }
            // XPutBackEvent prepends; restoring in reverse yields native FIFO
            // followed by synthetic FIFO, without changing any original event.
            for (auto& it : std::views::reverse(injected))
                (void)originalPutBack(target, &it);
            for (auto& it : std::views::reverse(native))
                (void)originalPutBack(target, &it);
            purgeCancelled(target);
            originalUnlockDisplay(target);
        }

        /**
         * @brief Retrieves an eligible native event before considering synthetic events.
         * @param target Application Xlib display.
         * @param check Nonblocking Xlib filter that removes an eligible event.
         * @return Whether an eligible event was retrieved.
         */
        template <typename Check>
        bool nextMatching(Display* target, const Check& check) {
            if (check()) {
                return true;
            }
            // An application may leave unrelated events queued while using a filtered API.
            // After no native event matches the filter, synthetic input may satisfy it.
            {
                std::scoped_lock lock(runtime().mutex);
                if (runtime().pending.empty() || runtime().activeX11) {
                    return false;
                }
            }
            pump(target);
            return check();
        }

        /** XPeekIfEvent must not remove/reorder matching or unrelated events. */
        bool peekMatching(Display* display, XEvent* result, Bool (*predicate)(Display*, XEvent*, XPointer), XPointer arg) {
            if ((originalPending == nullptr) || (originalNext == nullptr) || (originalPutBack == nullptr) || (originalLockDisplay == nullptr) || (originalUnlockDisplay == nullptr))
                return false;
            originalLockDisplay(display);
            const int count = originalPending(display);
            if (count < 0) {
                originalUnlockDisplay(display);
                return false;
            }
            std::vector<XEvent> events;
            bool                found = false;
            try {
                events.reserve(static_cast<size_t>(count));
                for (int i = 0; i < count; ++i) {
                    XEvent current{};
                    originalNext(display, &current);
                    events.push_back(current);
                }
                for (const auto& current : events) {
                    XEvent candidate = current;
                    if (predicate(display, &candidate, arg) != 0) {
                        *result = current;
                        found   = true;
                        break;
                    }
                }
            } catch (const std::bad_alloc&) {
                // Restore all events already removed, even when allocation fails.
                found = false;
            }
            for (auto& event : std::views::reverse(events))
                (void)originalPutBack(display, &event);
            originalUnlockDisplay(display);
            return found;
        }

        /**
         * @brief Processes synthetic events only after older Xlib events have been consumed.
         * @param target Application Xlib display.
         */
        void onEntry(Display* target) {
            if (getXcbConnection != nullptr)
                markXlibOwnedXcbConnection(getXcbConnection(target));
            if ((getXcbConnection != nullptr) && xcbOwnsXlibConnection(getXcbConnection(target)))
                return;
            if (originalCheckIf != nullptr) {
                XEvent wake{};
                while (originalCheckIf(target, &wake, &wakeEvent, nullptr) != 0) {}
            }
            purgeCancelled(target);
            promoteFocusTransition(target);
            // XPutBackEvent prepends events, so never overtake native events already queued.
            if ((originalPending != nullptr) && originalPending(target) == 0) {
                pump(target);
            }
        }

        /**
         * @brief Waits for X11 input or an IPC wakeup signal.
         * @param target Application Xlib display.
         */
        void waitForEvent(Display* target) {
            if (originalConnectionNumber == nullptr)
                return;
            auto waiter = std::make_shared<X11Waiter>();
            if (waiter->fd >= 0) {
                std::scoped_lock lock(wakeWaitersMutex);
                auto&            waiters = wakeWaitersStorage();
                std::erase_if(waiters, [](const auto& entry) { return entry.expired(); });
                waiters.emplace_back(waiter);
            }
            // Close the gap between an IPC enqueue and registering the waiter.
            // A pending task could have been signalled before registration.
            bool pending = false;
            {
                std::scoped_lock lock(runtime().mutex);
                if (!runtime().pending.empty()) {
                    const auto& selected = runtime().pending.front()->target;
                    pending              = selected.backend == HookBackend::X11 && selected.connection == target;
                }
            }
            if (pending) {
                onEntry(target);
                // Nonmatching native events were already in Xlib's queue.
                // They are not evidence that the pending synthetic burst was
                // inserted, and must not turn a filtered wait into a spin.
                std::scoped_lock lock(runtime().mutex);
                if (runtime().activeX11 && runtime().activeX11Display == target)
                    return;
            }
            std::array fds{
                pollfd{.fd = originalConnectionNumber(target), .events = POLLIN, .revents = 0},
                pollfd{.fd = waiter->fd, .events = POLLIN, .revents = 0},
            };
            // Another Xlib thread can consume the socket and queue a native
            // event after our empty-queue check. Its socket fd is no longer
            // readable, so use a bounded wait and let the caller recheck
            // Xlib's internal queue.
            const int ready = retryOnEintr([&] { return poll(fds.data(), fds.size(), 20); });
            if (ready > 0 && waiter->fd >= 0 && hasPollEvents(fds[1].revents, POLLIN)) {
                uint64_t value = 0;
                while (read(waiter->fd, &value, sizeof(value)) == sizeof(value)) {}
                onEntry(target);
            }
        }

    } // namespace

    void resetX11AfterFork() {
        // The old generation can contain live locks and partially updated maps.
        currentX11.store(nullptr, std::memory_order_release);
        new (&resolveOnce) std::once_flag;
    }

    void wakeX11() {
        // Do not activate an unused backend merely because another backend
        // asked the worker to broadcast a completion wakeup.
        auto* generation = currentX11.load(std::memory_order_acquire);
        if (generation == nullptr)
            return;
        {
            std::scoped_lock lock(wakeWaitersMutex);
            for (const auto& entry : generation->wakeWaiters) {
                if (auto waiter = entry.lock(); waiter && waiter->fd >= 0) {
                    const uint64_t one     = 1;
                    const ssize_t  written = write(waiter->fd, &one, sizeof(one));
                    (void)written;
                }
            }
        }
        // Wake external poll/select loops on the X connection itself. Sending
        // from a separate connection avoids calling Xlib from the IPC thread.
        if (originalDisplayString == nullptr || wakeConnect == nullptr || wakeDisconnect == nullptr || wakeSendEvent == nullptr || wakeFlush == nullptr ||
            wakeGetFocus == nullptr || wakeGetFocusReply == nullptr)
            return;
        std::vector<const void*> targets;
        {
            std::scoped_lock lock(runtime().mutex);
            for (const auto& task : runtime().pending)
                if (task->target.backend == HookBackend::X11)
                    targets.push_back(task->target.connection);
        }
        std::scoped_lock lock(focusMutex);
        for (const auto& [display, state] : focusByDisplay) {
            if (!state.focused || state.target == None || xcbOwnerDisplays.contains(display) || std::ranges::find(targets, display) == targets.end())
                continue;
            auto* sender = wakeConnect(originalDisplayString(display), nullptr);
            if (sender == nullptr)
                continue;
            xcb_client_message_event_t event{};
            event.response_type = XCB_CLIENT_MESSAGE;
            event.format        = 8;
            event.window        = static_cast<xcb_window_t>(state.target);
            event.type          = XCB_ATOM_NONE;
            std::memcpy(event.data.data8, kWakePayload.data(), kWakePayload.size());
            (void)wakeSendEvent(sender, 0, event.window, 0, reinterpret_cast<const char*>(&event));
            (void)wakeFlush(sender);
            // Wait until the server has processed the send before closing the
            // temporary connection; otherwise it can discard the wake event.
            freeReply(wakeGetFocusReply(sender, wakeGetFocus(sender), nullptr));
            wakeDisconnect(sender);
        }
    }

    FocusCandidates focusedX11Candidates() {
        if (currentX11.load(std::memory_order_acquire) == nullptr)
            return {};
        std::scoped_lock lock(focusMutex);
        FocusCandidates  candidates;
        for (const auto& [display, state] : focusByDisplay) {
            if (xcbOwnerDisplays.contains(display) || !state.ready() || !state.focused || state.target == None)
                continue;
            ++candidates.count;
            candidates.target = {.backend = HookBackend::X11, .connection = display, .focused = static_cast<uintptr_t>(state.target)};
        }
        return candidates;
    }

    void loadX11() {
        ensureRuntimeInitialized();
        std::call_once(resolveOnce, [] {
            xlib = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
            if (!xlib)
                return;
            xlibXcb = dlopen("libX11-xcb.so.1", RTLD_LAZY | RTLD_LOCAL);
            if (xlibXcb) {
                getXcbConnection           = reinterpret_cast<decltype(getXcbConnection)>(dlsym(xlibXcb, "XGetXCBConnection"));
                originalSetEventQueueOwner = reinterpret_cast<decltype(originalSetEventQueueOwner)>(dlsym(xlibXcb, "XSetEventQueueOwner"));
            }
            originalSelectInput            = resolve<decltype(originalSelectInput)>("XSelectInput");
            originalChangeWindowAttributes = resolve<decltype(originalChangeWindowAttributes)>("XChangeWindowAttributes");
            originalCreateWindow           = resolve<decltype(originalCreateWindow)>("XCreateWindow");
            originalNext                   = resolve<decltype(originalNext)>("XNextEvent");
            originalPeek                   = resolve<decltype(originalPeek)>("XPeekEvent");
            originalMask                   = resolve<decltype(originalMask)>("XMaskEvent");
            originalWindow                 = resolve<decltype(originalWindow)>("XWindowEvent");
            originalCheckMask              = resolve<decltype(originalCheckMask)>("XCheckMaskEvent");
            originalCheckWindow            = resolve<decltype(originalCheckWindow)>("XCheckWindowEvent");
            originalCheckTyped             = resolve<decltype(originalCheckTyped)>("XCheckTypedEvent");
            originalCheckTypedWindow       = resolve<decltype(originalCheckTypedWindow)>("XCheckTypedWindowEvent");
            originalIf                     = resolve<decltype(originalIf)>("XIfEvent");
            originalPeekIf                 = resolve<decltype(originalPeekIf)>("XPeekIfEvent");
            originalCheckIf                = resolve<decltype(originalCheckIf)>("XCheckIfEvent");
            originalPending                = resolve<decltype(originalPending)>("XPending");
            originalPutBack                = resolve<decltype(originalPutBack)>("XPutBackEvent");
            originalLockDisplay            = resolve<decltype(originalLockDisplay)>("XLockDisplay");
            originalUnlockDisplay          = resolve<decltype(originalUnlockDisplay)>("XUnlockDisplay");
            originalConnectionNumber       = resolve<decltype(originalConnectionNumber)>("XConnectionNumber");
            originalCloseDisplay           = resolve<decltype(originalCloseDisplay)>("XCloseDisplay");
            originalDestroyWindow          = resolve<decltype(originalDestroyWindow)>("XDestroyWindow");
            originalGetFocus               = resolve<decltype(originalGetFocus)>("XGetInputFocus");
            originalKeysymToKeycode        = resolve<decltype(originalKeysymToKeycode)>("XKeysymToKeycode");
            originalRootWindow             = resolve<decltype(originalRootWindow)>("XDefaultRootWindow");
            originalDisplayString          = resolve<decltype(originalDisplayString)>("XDisplayString");
            if (void* xcb = dlopen("libxcb.so.1", RTLD_NOW | RTLD_NOLOAD)) {
                wakeConnect       = reinterpret_cast<decltype(wakeConnect)>(dlsym(xcb, "xcb_connect"));
                wakeDisconnect    = reinterpret_cast<decltype(wakeDisconnect)>(dlsym(xcb, "xcb_disconnect"));
                wakeSendEvent     = reinterpret_cast<decltype(wakeSendEvent)>(dlsym(xcb, "xcb_send_event"));
                wakeFlush         = reinterpret_cast<decltype(wakeFlush)>(dlsym(xcb, "xcb_flush"));
                wakeGetFocus      = reinterpret_cast<decltype(wakeGetFocus)>(dlsym(xcb, "xcb_get_input_focus"));
                wakeGetFocusReply = reinterpret_cast<decltype(wakeGetFocusReply)>(dlsym(xcb, "xcb_get_input_focus_reply"));
            }
        });
    }

} // namespace lotus::apphook

// The preloaded C shim is the only externally visible interposer.
#define XSelectInput            lotus_apphook_impl_XSelectInput
#define XChangeWindowAttributes lotus_apphook_impl_XChangeWindowAttributes
#define XCreateWindow           lotus_apphook_impl_XCreateWindow
#define XSetEventQueueOwner     lotus_apphook_impl_XSetEventQueueOwner
#define XDestroyWindow          lotus_apphook_impl_XDestroyWindow
#define XCloseDisplay           lotus_apphook_impl_XCloseDisplay
#define XPending                lotus_apphook_impl_XPending
#define XNextEvent              lotus_apphook_impl_XNextEvent
#define XPeekEvent              lotus_apphook_impl_XPeekEvent
#define XMaskEvent              lotus_apphook_impl_XMaskEvent
#define XWindowEvent            lotus_apphook_impl_XWindowEvent
#define XCheckMaskEvent         lotus_apphook_impl_XCheckMaskEvent
#define XCheckWindowEvent       lotus_apphook_impl_XCheckWindowEvent
#define XCheckTypedEvent        lotus_apphook_impl_XCheckTypedEvent
#define XCheckTypedWindowEvent  lotus_apphook_impl_XCheckTypedWindowEvent
#define XIfEvent                lotus_apphook_impl_XIfEvent
#define XCheckIfEvent           lotus_apphook_impl_XCheckIfEvent
#define XPeekIfEvent            lotus_apphook_impl_XPeekIfEvent
#define LH_EXPORT               extern "C" __attribute__((visibility("hidden")))

/**
 * @brief Detects Xlib keyboard event subscriptions before keyboard focus arrives.
 * @param target Application Xlib display.
 * @param window X11 window subscribing to events.
 * @param mask Requested X11 event mask.
 * @return Original Xlib call status.
 */
LH_EXPORT int XSelectInput(Display* target, Window window, long mask) {
    using namespace lotus::apphook;
    loadX11();
    if (originalSelectInput == nullptr)
        return 0;
    if (getXcbConnection != nullptr)
        markXlibOwnedXcbConnection(getXcbConnection(target));
    const int result = originalSelectInput(target, window, mask);
    if (result != 0) {
        updateKeyboardMask(target, window, mask);
        if (selectsKeys(mask) && (getXcbConnection != nullptr))
            markXcbKeyboardSubscribed(getXcbConnection(target));
    }
    return result;
}

/**
 * @brief Detects keyboard event masks updated through XChangeWindowAttributes.
 * @param target Application Xlib display.
 * @param window X11 window being configured.
 * @param mask Attribute selection bitmask.
 * @param attributes New X11 window attributes.
 * @return Original Xlib call status.
 */
LH_EXPORT int XChangeWindowAttributes(Display* target, Window window, unsigned long mask, XSetWindowAttributes* attributes) {
    using namespace lotus::apphook;
    loadX11();
    if (originalChangeWindowAttributes == nullptr)
        return 0;
    if (getXcbConnection != nullptr)
        markXlibOwnedXcbConnection(getXcbConnection(target));
    const int result = originalChangeWindowAttributes(target, window, mask, attributes);
    if ((result != 0) && ((mask & CWEventMask) != 0U) && (attributes != nullptr)) {
        updateKeyboardMask(target, window, attributes->event_mask);
        if (selectsKeys(attributes->event_mask) && (getXcbConnection != nullptr))
            markXcbKeyboardSubscribed(getXcbConnection(target));
    }
    return result;
}

/**
 * @brief Detects keyboard event subscriptions installed while creating an Xlib window.
 * @param target Application Xlib display.
 * @param parent Parent X11 window.
 * @param x Window X coordinate.
 * @param y Window Y coordinate.
 * @param width Window width.
 * @param height Window height.
 * @param borderWidth Window border width.
 * @param depth Window pixel depth.
 * @param windowClass X11 window class.
 * @param visual Window visual.
 * @param mask Attribute selection bitmask.
 * @param attributes Initial X11 window attributes.
 * @return Created X11 window or None on failure.
 */
LH_EXPORT Window XCreateWindow(Display* target, Window parent, int x, int y, unsigned int width, unsigned int height, unsigned int borderWidth, int depth, unsigned int windowClass,
                               Visual* visual, unsigned long mask, XSetWindowAttributes* attributes) {
    using namespace lotus::apphook;
    loadX11();
    if (originalCreateWindow == nullptr)
        return None;
    if (getXcbConnection != nullptr)
        markXlibOwnedXcbConnection(getXcbConnection(target));
    const Window window = originalCreateWindow(target, parent, x, y, width, height, borderWidth, depth, windowClass, visual, mask, attributes);
    if (window != None && ((mask & CWEventMask) != 0U) && (attributes != nullptr)) {
        updateKeyboardMask(target, window, attributes->event_mask);
        if (selectsKeys(attributes->event_mask) && (getXcbConnection != nullptr))
            markXcbKeyboardSubscribed(getXcbConnection(target));
    }
    return window;
}

/** Syncs earlier Xlib/XCB event queue ownership without altering Xlib itself. */
LH_EXPORT void lotus_apphook_adopt_xlib_owner(Display* target, int owner) {
    using namespace lotus::apphook;
    loadX11();
    if (getXcbConnection != nullptr) {
        auto* connection = getXcbConnection(target);
        markXlibOwnedXcbConnection(connection);
        setXcbEventQueueOwner(connection, owner == 1, (originalRootWindow != nullptr) ? static_cast<uint32_t>(originalRootWindow(target)) : 0);
    }
    bool focused = false;
    bool ready   = false;
    {
        std::scoped_lock lock(focusMutex);
        if (owner == 1) {
            xcbOwnerDisplays.insert(target);
            focusByDisplay[target].focused = false;
            focusByDisplay[target].target  = None;
            focusByDisplay[target].focus   = None;
        } else {
            xcbOwnerDisplays.erase(target);
        }
        for (const auto& [display, state] : focusByDisplay)
            if (!xcbOwnerDisplays.contains(display))
                focused |= state.focused;
        ready = xlibReady();
    }
    noteX11Focus(focused);
    noteX11KeyboardAvailable(ready);
}

/** Tracks Xlib/XCB event queue ownership (XCBOwnsEventQueue is 1). */
LH_EXPORT void XSetEventQueueOwner(Display* target, int owner) {
    using namespace lotus::apphook;
    loadX11();
    if (originalSetEventQueueOwner != nullptr)
        originalSetEventQueueOwner(target, owner);
    lotus_apphook_adopt_xlib_owner(target, owner);
}

/** Forgets destroyed windows without leaving stale keyboard subscriptions. */
LH_EXPORT int XDestroyWindow(Display* target, Window window) {
    using namespace lotus::apphook;
    loadX11();
    if (originalDestroyWindow == nullptr)
        return 0;
    const int result = originalDestroyWindow(target, window);
    if (result != 0)
        forgetWindow(target, window);
    return result;
}

/** Removes focus state and rejects undelivered keys when a Display is closed. */
LH_EXPORT int XCloseDisplay(Display* target) {
    using namespace lotus::apphook;
    loadX11();
    if (getXcbConnection != nullptr)
        forgetXlibXcbConnection(getXcbConnection(target));
    bool focused = false;
    bool ready   = false;
    {
        std::scoped_lock lock(focusMutex);
        focusByDisplay.erase(target);
        xcbOwnerDisplays.erase(target);
        for (const auto& [display, state] : focusByDisplay)
            if (!xcbOwnerDisplays.contains(display))
                focused |= state.focused;
        ready = xlibReady();
    }
    noteX11Focus(focused);
    noteX11KeyboardAvailable(ready);
    cancelPendingTasks(HookBackend::X11, target);
    {
        std::scoped_lock lock(runtime().mutex);
        if (runtime().activeX11 && runtime().activeX11Display == target) {
            finishLocked(runtime().activeX11, -ENOTCONN);
            runtime().activeX11.reset();
            runtime().activeX11Display = nullptr;
            pressedEvents.erase(target);
        }
    }
    return (originalCloseDisplay != nullptr) ? originalCloseDisplay(target) : -1;
}

/**
 * @brief Makes application-local input visible to XPending-driven event loops.
 * Native queued events continue to precede synthetic input.
 */
LH_EXPORT int XPending(Display* target) {
    using namespace lotus::apphook;
    loadX11();
    if (originalPending == nullptr)
        return 0;
    onEntry(target);
    return originalPending(target);
}

/**
 * @brief Intercepts the blocking Xlib next-event call.
 * @param target Application Xlib display.
 * @param event Output event storage.
 * @return Original Xlib call status.
 */
LH_EXPORT int XNextEvent(Display* target, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalNext == nullptr)
        return -1;
    while (true) {
        onEntry(target);
        if ((originalPending != nullptr) && (originalConnectionNumber != nullptr) && runtime().workersStarted.load(std::memory_order_acquire)) {
            while (originalPending(target) == 0)
                waitForEvent(target);
        }
        const int result = originalNext(target, event);
        if (result == 0 && isWakeEvent(event))
            continue;
        if (result == 0)
            recordEvent(*event, true);
        return result;
    }
}

/**
 * @brief Intercepts the blocking Xlib peek-event call.
 * @param target Application Xlib display.
 * @param event Output event storage.
 * @return Original Xlib call status.
 */
LH_EXPORT int XPeekEvent(Display* target, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalPeek == nullptr)
        return -1;
    onEntry(target);
    if ((originalPending != nullptr) && (originalConnectionNumber != nullptr) && runtime().workersStarted.load(std::memory_order_acquire)) {
        while (originalPending(target) == 0)
            waitForEvent(target);
    }
    return originalPeek(target, event);
}

/**
 * @brief Intercepts the masked Xlib event retrieval call.
 * @param target Application Xlib display.
 * @param mask Event selection mask.
 * @param event Output event storage.
 * @return Original Xlib call status.
 */
LH_EXPORT int XMaskEvent(Display* target, long mask, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalMask == nullptr)
        return -1;
    onEntry(target);
    if ((originalCheckMask != nullptr) && (originalConnectionNumber != nullptr) && runtime().workersStarted.load(std::memory_order_acquire)) {
        while (!nextMatching(target, [&] { return originalCheckMask(target, mask, event); }))
            waitForEvent(target);
        recordEvent(*event, true);
        return 0;
    }
    const int result = originalMask(target, mask, event);
    if (result == 0)
        recordEvent(*event, true);
    return result;
}

/**
 * @brief Intercepts the window-specific Xlib event retrieval call.
 * @param target Application Xlib display.
 * @param window Target X11 window.
 * @param mask Event selection mask.
 * @param event Output event storage.
 * @return Original Xlib call status.
 */
LH_EXPORT int XWindowEvent(Display* target, Window window, long mask, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalWindow == nullptr)
        return -1;
    onEntry(target);
    if ((originalCheckWindow != nullptr) && (originalConnectionNumber != nullptr) && runtime().workersStarted.load(std::memory_order_acquire)) {
        while (!nextMatching(target, [&] { return originalCheckWindow(target, window, mask, event); }))
            waitForEvent(target);
        recordEvent(*event, true);
        return 0;
    }
    const int result = originalWindow(target, window, mask, event);
    if (result == 0)
        recordEvent(*event, true);
    return result;
}

/**
 * @brief Intercepts nonblocking masked Xlib event retrieval.
 * @param target Application Xlib display.
 * @param mask Event selection mask.
 * @param event Output event storage.
 * @return Whether an event was returned.
 */
LH_EXPORT Bool XCheckMaskEvent(Display* target, long mask, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalCheckMask == nullptr)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckMask(target, mask, event); }) ? True : False;
    if (found != 0)
        recordEvent(*event, true);
    return found;
}

/**
 * @brief Intercepts nonblocking window-specific Xlib retrieval.
 * @param target Application Xlib display.
 * @param window Target X11 window.
 * @param mask Event selection mask.
 * @param event Output event storage.
 * @return Whether an event was returned.
 */
LH_EXPORT Bool XCheckWindowEvent(Display* target, Window window, long mask, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalCheckWindow == nullptr)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckWindow(target, window, mask, event); }) ? True : False;
    if (found != 0)
        recordEvent(*event, true);
    return found;
}

/**
 * @brief Intercepts nonblocking Xlib retrieval by event type.
 * @param target Application Xlib display.
 * @param type Requested event type.
 * @param event Output event storage.
 * @return Whether an event was returned.
 */
LH_EXPORT Bool XCheckTypedEvent(Display* target, int type, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalCheckTyped == nullptr)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckTyped(target, type, event); }) ? True : False;
    if (found != 0)
        recordEvent(*event, true);
    return found;
}

/**
 * @brief Intercepts nonblocking Xlib retrieval by window and type.
 * @param target Application Xlib display.
 * @param window Target X11 window.
 * @param type Requested event type.
 * @param event Output event storage.
 * @return Whether an event was returned.
 */
LH_EXPORT Bool XCheckTypedWindowEvent(Display* target, Window window, int type, XEvent* event) {
    using namespace lotus::apphook;
    loadX11();
    if (originalCheckTypedWindow == nullptr)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckTypedWindow(target, window, type, event); }) ? True : False;
    if (found != 0)
        recordEvent(*event, true);
    return found;
}

/**
 * @brief Intercepts predicate-based blocking Xlib event retrieval.
 * @param target Application Xlib display.
 * @param event Output event storage.
 * @param predicate Xlib event selection predicate.
 * @param arg Predicate context.
 * @return Original Xlib call status.
 */
LH_EXPORT int XIfEvent(Display* target, XEvent* event, Bool (*predicate)(Display*, XEvent*, XPointer), XPointer arg) {
    using namespace lotus::apphook;
    loadX11();
    if (originalIf == nullptr)
        return -1;
    onEntry(target);
    if ((originalCheckIf != nullptr) && (originalConnectionNumber != nullptr) && runtime().workersStarted.load(std::memory_order_acquire)) {
        while (!nextMatching(target, [&] { return originalCheckIf(target, event, predicate, arg); }))
            waitForEvent(target);
        recordEvent(*event, true);
        return 0;
    }
    const int result = originalIf(target, event, predicate, arg);
    if (result == 0)
        recordEvent(*event, true);
    return result;
}

/**
 * @brief Intercepts nonblocking predicate-based Xlib retrieval.
 * @param target Application Xlib display.
 * @param event Output event storage.
 * @param predicate Xlib event selection predicate.
 * @param arg Predicate context.
 * @return Whether an event was returned.
 */
LH_EXPORT Bool XCheckIfEvent(Display* target, XEvent* event, Bool (*predicate)(Display*, XEvent*, XPointer), XPointer arg) {
    using namespace lotus::apphook;
    loadX11();
    if (originalCheckIf == nullptr)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckIf(target, event, predicate, arg); }) ? True : False;
    if (found != 0)
        recordEvent(*event, true);
    return found;
}

/** Preserves the predicate-based peek API without consuming any native event. */
LH_EXPORT int XPeekIfEvent(Display* target, XEvent* event, Bool (*predicate)(Display*, XEvent*, XPointer), XPointer arg) {
    using namespace lotus::apphook;
    loadX11();
    if (originalPeekIf == nullptr)
        return -1;
    onEntry(target);
    if ((predicate == nullptr) || (originalPending == nullptr) || (originalConnectionNumber == nullptr) || !runtime().workersStarted.load(std::memory_order_acquire))
        return originalPeekIf(target, event, predicate, arg);
    while (true) {
        if (originalPending(target) < 0)
            return originalPeekIf(target, event, predicate, arg);
        if (peekMatching(target, event, predicate, arg))
            return 0;
        // Filtered peeks must also see tasks behind unrelated native events.
        pump(target);
        if (peekMatching(target, event, predicate, arg))
            return 0;
        waitForEvent(target);
    }
}
