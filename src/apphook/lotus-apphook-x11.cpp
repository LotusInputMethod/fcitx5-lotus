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

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <linux/input-event-codes.h>
#include <mutex>
#include <poll.h>
#include <vector>
#include <unordered_map>
#include <unistd.h>

namespace lotus::apphook {
    namespace {

        constexpr unsigned long kInjectedSerial = 0x4c485831UL;

        struct DisplayFocus {
            Window targetWindow  = None;
            Window focusWindow   = None;
            bool   observedFocus = false;
            bool   focused       = false;
        };
        std::mutex                                 focusMutex;
        std::unordered_map<Display*, DisplayFocus> focusByDisplay;
        void*                                      xlib    = nullptr;
        void*                                      xlibXcb = nullptr;
        void* (*getXcbConnection)(Display*)                = nullptr;
        void (*originalSetEventQueueOwner)(Display*, int)  = nullptr;
        std::mutex resolveMutex;

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
        Bool (*originalCheckIf)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer)   = nullptr;
        int (*originalPending)(Display*)                                                              = nullptr;
        int (*originalPutBack)(Display*, XEvent*)                                                     = nullptr;
        void (*originalLockDisplay)(Display*)                                                         = nullptr;
        void (*originalUnlockDisplay)(Display*)                                                       = nullptr;
        int (*originalCloseDisplay)(Display*)                                                         = nullptr;
        int (*originalConnectionNumber)(Display*)                                                     = nullptr;
        Status (*originalGetFocus)(Display*, Window*, int*)                                           = nullptr;
        KeyCode (*originalKeysymToKeycode)(Display*, KeySym)                                          = nullptr;
        Window (*originalRootWindow)(Display*)                                                        = nullptr;

        /**
         * @brief Looks up an original Xlib function.
         * @param name Xlib dynamic symbol name.
         * @return Typed original Xlib function address.
         */
        template <typename Function>
        Function resolve(const char* name) {
            return reinterpret_cast<Function>(dlsym(xlib, name));
        }

        /** @brief Matches application-local synthetic events in the Xlib queue. */
        Bool syntheticEvent(Display*, XEvent* event, XPointer) {
            return (event->type == KeyPress || event->type == KeyRelease) && event->xkey.serial == kInjectedSerial;
        }

        /** @brief Removes a cancelled task's undelivered events on the application thread. */
        void purgeCancelled(Display* target) {
            Runtime&              state = runtime();
            std::shared_ptr<Task> task;
            {
                std::scoped_lock lock(state.mutex);
                task = state.activeX11;
                if (!task || state.activeX11Display != target || !task->cancelled.load(std::memory_order_acquire))
                    return;
            }
            if (originalCheckIf) {
                XEvent discarded{};
                while (originalCheckIf(target, &discarded, &syntheticEvent, nullptr)) {
                    std::scoped_lock lock(state.mutex);
                    if (task->x11Remaining > 0)
                        --task->x11Remaining;
                }
            }
            std::scoped_lock lock(state.mutex);
            if (state.activeX11 == task && task->x11Remaining == 0) {
                finishLocked(task, -ECANCELED);
                state.activeX11.reset();
                state.activeX11Display = nullptr;
            }
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
                {
                    std::scoped_lock lock(focusMutex);
                    auto&            tracked = focusByDisplay[event.xfocus.display];
                    if (event.type == FocusIn) {
                        tracked.targetWindow = event.xfocus.window;
                        tracked.focusWindow  = event.xfocus.window;
                        tracked.focused      = true;
                    } else if (tracked.focusWindow == event.xfocus.window) {
                        tracked.targetWindow = None;
                        tracked.focusWindow  = None;
                        tracked.focused      = false;
                    }
                    tracked.observedFocus = true;
                    for (const auto& [ignored, state] : focusByDisplay)
                        focused |= state.focused;
                }
                runtime().observedX11Focus.store(true, std::memory_order_release);
                noteX11Focus(focused);
            }
            if ((event.type == KeyPress || event.type == KeyRelease) && event.xkey.serial == kInjectedSerial) {
                Runtime&         state = runtime();
                std::scoped_lock lock(state.mutex);
                if (state.activeX11 && state.activeX11Display == event.xkey.display && state.activeX11->x11Remaining > 0) {
                    auto task = state.activeX11;
                    task->applied.store(true, std::memory_order_release);
                    if (--task->x11Remaining == 0) {
                        finishLocked(task, task->cancelled.load(std::memory_order_acquire) ? -ECANCELED : 0);
                        state.activeX11.reset();
                        state.activeX11Display = nullptr;
                    }
                }
            }
            if ((event.type == KeyPress || event.type == KeyRelease) && event.xkey.serial != kInjectedSerial) {
                // Real keyboard events identify the window that handles keys.
                // Other X11 event classes must never change the injection target.
                {
                    std::scoped_lock lock(focusMutex);
                    focusByDisplay[event.xkey.display].targetWindow = event.xkey.window;
                }
                runtime().readyX11.store(true, std::memory_order_release);
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
            if (symbol != NoSymbol && originalKeysymToKeycode) {
                const KeyCode resolved = originalKeysymToKeycode(target, symbol);
                if (resolved) {
                    return resolved;
                }
            }
            return static_cast<KeyCode>(evdev + 8);
        }

        /**
         * @brief Converts Lotus modifier bits into X11 modifier masks.
         * @param bits Lotus modifier bits.
         * @return X11 modifier state.
         */
        unsigned modifiers(uint32_t bits) {
            return ((bits & kModifierShift) ? ShiftMask : 0) | ((bits & kModifierCtrl) ? ControlMask : 0) | ((bits & kModifierAlt) ? Mod1Mask : 0) |
                ((bits & kModifierSuper) ? Mod4Mask : 0);
        }

        /**
         * @brief Appends synthetic keyboard events after already-queued native events.
         * @param target Application Xlib display.
         */
        void pump(Display* target) {
            if (!target || !originalPutBack || !originalNext || !originalPending || !originalLockDisplay || !originalUnlockDisplay)
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
                    window = it->second.targetWindow;
                }
            }
            if (window == None) {
                Window focused = None;
                int    revert  = 0;
                if (!originalGetFocus || !originalGetFocus(target, &focused, &revert) || focused == None || focused == PointerRoot)
                    return;
                window = focused;
                std::scoped_lock lock(focusMutex);
                focusByDisplay[target].targetWindow = focused;
            }
            runtime().readyX11.store(true, std::memory_order_release);

            // Keep the native queue order intact even when an XCheck* filter
            // ignores an unrelated event at its head. Serialise Xlib calls so
            // other application threads cannot observe the temporary drain.
            originalLockDisplay(target);
            const int queued = originalPending(target);
            if (queued < 0 || queued > 4096) {
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
                const bool focusTransition = std::any_of(native.begin(), native.end(), [](const XEvent& event) { return event.type == FocusIn || event.type == FocusOut; });
                if (!focusTransition) {
                    auto task = takeTask();
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
                        for (const auto& op : task->ops) {
                            const KeyCode code = keycode(target, op.keycode);
                            for (uint32_t i = 0; i < op.count; ++i) {
                                for (int edge = 0; edge < 2; ++edge) {
                                    XEvent event{};
                                    event.xkey.type        = edge ? KeyRelease : KeyPress;
                                    event.xkey.display     = target;
                                    event.xkey.window      = window;
                                    event.xkey.root        = originalRootWindow ? originalRootWindow(target) : window;
                                    event.xkey.time        = CurrentTime;
                                    event.xkey.keycode     = code;
                                    event.xkey.state       = modifiers(op.modifiers);
                                    event.xkey.same_screen = True;
                                    event.xkey.send_event  = True;
                                    event.xkey.serial      = kInjectedSerial;
                                    injected.push_back(event);
                                }
                            }
                        }
                        if (task->cancelled.load(std::memory_order_acquire)) {
                            std::scoped_lock lock(runtime().mutex);
                            finishLocked(task, -ECANCELED);
                            runtime().activeX11.reset();
                            runtime().activeX11Display = nullptr;
                            injected.clear();
                        } else {
                            std::scoped_lock lock(runtime().mutex);
                            task->x11Remaining = injected.size();
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
            for (auto it = injected.rbegin(); it != injected.rend(); ++it)
                originalPutBack(target, &*it);
            for (auto it = native.rbegin(); it != native.rend(); ++it)
                originalPutBack(target, &*it);
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
        bool nextMatching(Display* target, Check&& check) {
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

        /**
         * @brief Processes synthetic events only after older Xlib events have been consumed.
         * @param target Application Xlib display.
         */
        void onEntry(Display* target) {
            if (getXcbConnection)
                markXlibOwnedXcbConnection(getXcbConnection(target));
            if (getXcbConnection && xcbOwnsXlibConnection(getXcbConnection(target)))
                return;
            purgeCancelled(target);
            // XPutBackEvent prepends events, so never overtake native events already queued.
            if (originalPending && originalPending(target) == 0) {
                pump(target);
            }
        }

        /**
         * @brief Waits for X11 input or an IPC wakeup signal.
         * @param target Application Xlib display.
         */
        void waitForEvent(Display* target) {
            const int fd = runtime().wakeFd.load(std::memory_order_acquire);
            if (!originalConnectionNumber || fd < 0) {
                return;
            }
            pollfd fds[2]{{.fd = originalConnectionNumber(target), .events = POLLIN, .revents = 0}, {.fd = fd, .events = POLLIN, .revents = 0}};
            int    ready;
            do {
                ready = poll(fds, 2, -1);
            } while (ready < 0 && errno == EINTR);
            if (ready > 0 && (fds[1].revents & POLLIN)) {
                uint64_t value;
                while (read(fd, &value, sizeof(value)) == sizeof(value)) {}
                onEntry(target);
            }
        }

    } // namespace

    void loadX11() {
        std::scoped_lock lock(resolveMutex);
        if (xlib) {
            return;
        }
        xlib = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
        if (!xlib) {
            return;
        }
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
        originalCheckIf                = resolve<decltype(originalCheckIf)>("XCheckIfEvent");
        originalPending                = resolve<decltype(originalPending)>("XPending");
        originalPutBack                = resolve<decltype(originalPutBack)>("XPutBackEvent");
        originalLockDisplay            = resolve<decltype(originalLockDisplay)>("XLockDisplay");
        originalUnlockDisplay          = resolve<decltype(originalUnlockDisplay)>("XUnlockDisplay");
        originalConnectionNumber       = resolve<decltype(originalConnectionNumber)>("XConnectionNumber");
        originalCloseDisplay           = resolve<decltype(originalCloseDisplay)>("XCloseDisplay");
        originalGetFocus               = resolve<decltype(originalGetFocus)>("XGetInputFocus");
        originalKeysymToKeycode        = resolve<decltype(originalKeysymToKeycode)>("XKeysymToKeycode");
        originalRootWindow             = resolve<decltype(originalRootWindow)>("XDefaultRootWindow");
    }

} // namespace lotus::apphook

#define LH_EXPORT extern "C" __attribute__((visibility("default")))

/**
 * @brief Detects Xlib keyboard event subscriptions before keyboard focus arrives.
 * @param target Application Xlib display.
 * @param window X11 window subscribing to events.
 * @param mask Requested X11 event mask.
 * @return Original Xlib call status.
 */
LH_EXPORT int XSelectInput(Display* target, Window window, long mask) {
    using namespace lotus::apphook;
    if (!originalSelectInput)
        loadX11();
    if (!originalSelectInput)
        return 0;
    if (getXcbConnection)
        markXlibOwnedXcbConnection(getXcbConnection(target));
    const int result = originalSelectInput(target, window, mask);
    if (result && (mask & (KeyPressMask | KeyReleaseMask))) {
        noteX11KeyboardAvailable();
        if (getXcbConnection)
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
    if (!originalChangeWindowAttributes)
        loadX11();
    if (!originalChangeWindowAttributes)
        return 0;
    if (getXcbConnection)
        markXlibOwnedXcbConnection(getXcbConnection(target));
    const int result = originalChangeWindowAttributes(target, window, mask, attributes);
    if (result && (mask & CWEventMask) && attributes && (attributes->event_mask & (KeyPressMask | KeyReleaseMask))) {
        noteX11KeyboardAvailable();
        if (getXcbConnection)
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
    if (!originalCreateWindow)
        loadX11();
    if (!originalCreateWindow)
        return None;
    if (getXcbConnection)
        markXlibOwnedXcbConnection(getXcbConnection(target));
    const Window window = originalCreateWindow(target, parent, x, y, width, height, borderWidth, depth, windowClass, visual, mask, attributes);
    if (window != None && (mask & CWEventMask) && attributes && (attributes->event_mask & (KeyPressMask | KeyReleaseMask))) {
        noteX11KeyboardAvailable();
        if (getXcbConnection)
            markXcbKeyboardSubscribed(getXcbConnection(target));
    }
    return window;
}

/** Tracks Xlib/XCB event queue ownership (XCBOwnsEventQueue is 1). */
LH_EXPORT void XSetEventQueueOwner(Display* target, int owner) {
    using namespace lotus::apphook;
    if (!originalSetEventQueueOwner)
        loadX11();
    if (originalSetEventQueueOwner)
        originalSetEventQueueOwner(target, owner);
    if (getXcbConnection) {
        auto* connection = getXcbConnection(target);
        markXlibOwnedXcbConnection(connection);
        setXcbEventQueueOwner(connection, owner == 1);
    }
}

/** Removes focus state and rejects undelivered keys when a Display is closed. */
LH_EXPORT int XCloseDisplay(Display* target) {
    using namespace lotus::apphook;
    if (!originalCloseDisplay)
        loadX11();
    if (getXcbConnection)
        forgetXlibXcbConnection(getXcbConnection(target));
    bool focused = false;
    {
        std::scoped_lock lock(focusMutex);
        focusByDisplay.erase(target);
        for (const auto& [ignored, state] : focusByDisplay)
            focused |= state.focused;
    }
    noteX11Focus(focused);
    {
        std::scoped_lock lock(runtime().mutex);
        if (runtime().activeX11 && runtime().activeX11Display == target) {
            finishLocked(runtime().activeX11, -ENOTCONN);
            runtime().activeX11.reset();
            runtime().activeX11Display = nullptr;
        }
    }
    return originalCloseDisplay ? originalCloseDisplay(target) : -1;
}

/**
 * @brief Intercepts the blocking Xlib next-event call.
 * @param target Application Xlib display.
 * @param event Output event storage.
 * @return Original Xlib call status.
 */
LH_EXPORT int XNextEvent(Display* target, XEvent* event) {
    using namespace lotus::apphook;
    if (!originalNext)
        loadX11();
    if (!originalNext)
        return -1;
    onEntry(target);
    if (originalPending && originalConnectionNumber && runtime().wakeFd >= 0) {
        while (originalPending(target) == 0)
            waitForEvent(target);
    }
    const int result = originalNext(target, event);
    if (result == 0)
        recordEvent(*event, true);
    return result;
}

/**
 * @brief Intercepts the blocking Xlib peek-event call.
 * @param target Application Xlib display.
 * @param event Output event storage.
 * @return Original Xlib call status.
 */
LH_EXPORT int XPeekEvent(Display* target, XEvent* event) {
    using namespace lotus::apphook;
    if (!originalPeek)
        loadX11();
    if (!originalPeek)
        return -1;
    onEntry(target);
    if (originalPending && originalConnectionNumber && runtime().wakeFd >= 0) {
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
    if (!originalMask)
        loadX11();
    if (!originalMask)
        return -1;
    onEntry(target);
    if (originalCheckMask && originalConnectionNumber && runtime().wakeFd >= 0) {
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
    if (!originalWindow)
        loadX11();
    if (!originalWindow)
        return -1;
    onEntry(target);
    if (originalCheckWindow && originalConnectionNumber && runtime().wakeFd >= 0) {
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
    if (!originalCheckMask)
        loadX11();
    if (!originalCheckMask)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckMask(target, mask, event); });
    if (found)
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
    if (!originalCheckWindow)
        loadX11();
    if (!originalCheckWindow)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckWindow(target, window, mask, event); });
    if (found)
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
    if (!originalCheckTyped)
        loadX11();
    if (!originalCheckTyped)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckTyped(target, type, event); });
    if (found)
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
    if (!originalCheckTypedWindow)
        loadX11();
    if (!originalCheckTypedWindow)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckTypedWindow(target, window, type, event); });
    if (found)
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
    if (!originalIf)
        loadX11();
    if (!originalIf)
        return -1;
    onEntry(target);
    if (originalCheckIf && originalConnectionNumber && runtime().wakeFd >= 0) {
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
    if (!originalCheckIf)
        loadX11();
    if (!originalCheckIf)
        return False;
    onEntry(target);
    const Bool found = nextMatching(target, [&] { return originalCheckIf(target, event, predicate, arg); });
    if (found)
        recordEvent(*event, true);
    return found;
}

namespace lotus::apphook {

    int installX11Hooks() {
        if (!xlib)
            return 0;
        const std::array hooks{
            HookSpec{"XSetEventQueueOwner", reinterpret_cast<void*>(&XSetEventQueueOwner)},
            HookSpec{"XCloseDisplay", reinterpret_cast<void*>(&XCloseDisplay)},
            HookSpec{"XSelectInput", reinterpret_cast<void*>(&XSelectInput)},
            HookSpec{"XChangeWindowAttributes", reinterpret_cast<void*>(&XChangeWindowAttributes)},
            HookSpec{"XCreateWindow", reinterpret_cast<void*>(&XCreateWindow)},
            HookSpec{"XNextEvent", reinterpret_cast<void*>(&XNextEvent)},
            HookSpec{"XPeekEvent", reinterpret_cast<void*>(&XPeekEvent)},
            HookSpec{"XMaskEvent", reinterpret_cast<void*>(&XMaskEvent)},
            HookSpec{"XWindowEvent", reinterpret_cast<void*>(&XWindowEvent)},
            HookSpec{"XCheckMaskEvent", reinterpret_cast<void*>(&XCheckMaskEvent)},
            HookSpec{"XCheckWindowEvent", reinterpret_cast<void*>(&XCheckWindowEvent)},
            HookSpec{"XCheckTypedEvent", reinterpret_cast<void*>(&XCheckTypedEvent)},
            HookSpec{"XCheckTypedWindowEvent", reinterpret_cast<void*>(&XCheckTypedWindowEvent)},
            HookSpec{"XIfEvent", reinterpret_cast<void*>(&XIfEvent)},
            HookSpec{"XCheckIfEvent", reinterpret_cast<void*>(&XCheckIfEvent)},
        };
        return installHooks(hooks);
    }

} // namespace lotus::apphook
