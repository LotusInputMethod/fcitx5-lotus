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
#include <unistd.h>

namespace lotus::apphook {
    namespace {

        constexpr unsigned long kInjectedSerial = 0x4c485831UL;

        Display*                display      = nullptr;
        Window                  targetWindow = None;
        void*                   xlib         = nullptr;
        std::mutex              resolveMutex;

        int (*originalNext)(Display*, XEvent*)                                                      = nullptr;
        int (*originalPeek)(Display*, XEvent*)                                                      = nullptr;
        int (*originalMask)(Display*, long, XEvent*)                                                = nullptr;
        int (*originalWindow)(Display*, Window, long, XEvent*)                                      = nullptr;
        Bool (*originalCheckMask)(Display*, long, XEvent*)                                          = nullptr;
        Bool (*originalCheckWindow)(Display*, Window, long, XEvent*)                                = nullptr;
        Bool (*originalCheckTyped)(Display*, int, XEvent*)                                          = nullptr;
        Bool (*originalCheckTypedWindow)(Display*, Window, int, XEvent*)                            = nullptr;
        int (*originalIf)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer)       = nullptr;
        Bool (*originalCheckIf)(Display*, XEvent*, Bool (*)(Display*, XEvent*, XPointer), XPointer) = nullptr;
        int (*originalPending)(Display*)                                                            = nullptr;
        int (*originalPutBack)(Display*, XEvent*)                                                   = nullptr;
        int (*originalConnectionNumber)(Display*)                                                   = nullptr;
        Status (*originalGetFocus)(Display*, Window*, int*)                                         = nullptr;
        KeyCode (*originalKeysymToKeycode)(Display*, KeySym)                                        = nullptr;
        Window (*originalRootWindow)(Display*)                                                      = nullptr;

        /**
         * @brief Looks up an original Xlib function.
         * @param name Xlib dynamic symbol name.
         * @return Typed original Xlib function address.
         */
        template <typename Function>
        Function resolve(const char* name) {
            return reinterpret_cast<Function>(dlsym(xlib, name));
        }

        /**
         * @brief Acknowledges a batch after all synthetic X11 events have been consumed.
         */
        void acknowledgeCompleted() {
            Runtime&         state = runtime();
            std::scoped_lock lock(state.mutex);
            if (state.activeX11 && state.activeX11->x11Remaining == 0) {
                finishLocked(state.activeX11, 0);
                state.activeX11.reset();
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
            if (event.type == FocusIn) {
                noteX11Focus(true);
            } else if (event.type == FocusOut) {
                noteX11Focus(false);
            }
            if ((event.type == KeyPress || event.type == KeyRelease) && event.xkey.serial == kInjectedSerial) {
                Runtime&         state = runtime();
                std::scoped_lock lock(state.mutex);
                if (state.activeX11 && state.activeX11->x11Remaining > 0) {
                    --state.activeX11->x11Remaining;
                }
            } else if (event.xany.window != None) {
                targetWindow = event.xany.window;
                runtime().readyX11.store(true, std::memory_order_release);
            }
            if (event.type == KeyPress && event.xkey.serial != kInjectedSerial) {
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
         * @brief Prepends queued synthetic keyboard events to the Xlib event queue.
         * @param target Application Xlib display.
         */
        void pump(Display* target) {
            if (!target || !originalPutBack) {
                return;
            }
            {
                std::scoped_lock lock(runtime().mutex);
                if (runtime().activeX11) {
                    return;
                }
            }
            if (!targetWindow) {
                Window focused = None;
                int    revert  = 0;
                if (originalGetFocus && originalGetFocus(target, &focused, &revert) && focused != None && focused != PointerRoot) {
                    targetWindow = focused;
                }
            }
            if (!targetWindow) {
                return;
            }
            runtime().readyX11.store(true, std::memory_order_release);
            auto task = takeTask();
            if (!task) {
                return;
            }
            {
                std::scoped_lock lock(runtime().mutex);
                runtime().activeX11 = task;
            }

            try {
                size_t taps = 0;
                for (const auto& op : task->ops) {
                    taps += op.count;
                }
                std::vector<XEvent> events;
                events.reserve(taps * 2);
                for (const auto& op : task->ops) {
                    const KeyCode code = keycode(target, op.keycode);
                    for (uint32_t i = 0; i < op.count; ++i) {
                        for (int edge = 0; edge < 2; ++edge) {
                            XEvent event{};
                            event.xkey.type        = edge ? KeyRelease : KeyPress;
                            event.xkey.display     = target;
                            event.xkey.window      = targetWindow;
                            event.xkey.root        = originalRootWindow ? originalRootWindow(target) : targetWindow;
                            event.xkey.time        = CurrentTime;
                            event.xkey.keycode     = code;
                            event.xkey.state       = modifiers(op.modifiers);
                            event.xkey.same_screen = True;
                            event.xkey.send_event  = True;
                            event.xkey.serial      = kInjectedSerial;
                            events.push_back(event);
                        }
                    }
                }
                {
                    std::scoped_lock lock(runtime().mutex);
                    task->x11Remaining = events.size();
                }
                // XPutBackEvent prepends, so insert in reverse order.
                for (auto it = events.rbegin(); it != events.rend(); ++it) {
                    originalPutBack(target, &*it);
                }
            } catch (const std::bad_alloc&) {
                std::scoped_lock lock(runtime().mutex);
                finishLocked(task, -ENOMEM);
                runtime().activeX11.reset();
            }
        }

        /**
         * @brief Processes queued events when an application enters the Xlib event loop.
         * @param target Application Xlib display.
         */
        void onEntry(Display* target) {
            display = target;
            acknowledgeCompleted();
            pump(target);
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
        originalNext             = resolve<decltype(originalNext)>("XNextEvent");
        originalPeek             = resolve<decltype(originalPeek)>("XPeekEvent");
        originalMask             = resolve<decltype(originalMask)>("XMaskEvent");
        originalWindow           = resolve<decltype(originalWindow)>("XWindowEvent");
        originalCheckMask        = resolve<decltype(originalCheckMask)>("XCheckMaskEvent");
        originalCheckWindow      = resolve<decltype(originalCheckWindow)>("XCheckWindowEvent");
        originalCheckTyped       = resolve<decltype(originalCheckTyped)>("XCheckTypedEvent");
        originalCheckTypedWindow = resolve<decltype(originalCheckTypedWindow)>("XCheckTypedWindowEvent");
        originalIf               = resolve<decltype(originalIf)>("XIfEvent");
        originalCheckIf          = resolve<decltype(originalCheckIf)>("XCheckIfEvent");
        originalPending          = resolve<decltype(originalPending)>("XPending");
        originalPutBack          = resolve<decltype(originalPutBack)>("XPutBackEvent");
        originalConnectionNumber = resolve<decltype(originalConnectionNumber)>("XConnectionNumber");
        originalGetFocus         = resolve<decltype(originalGetFocus)>("XGetInputFocus");
        originalKeysymToKeycode  = resolve<decltype(originalKeysymToKeycode)>("XKeysymToKeycode");
        originalRootWindow       = resolve<decltype(originalRootWindow)>("XDefaultRootWindow");
    }

} // namespace lotus::apphook

#define LH_EXPORT extern "C" __attribute__((visibility("default")))

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
        while (!originalCheckMask(target, mask, event))
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
        while (!originalCheckWindow(target, window, mask, event))
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
    const Bool found = originalCheckMask(target, mask, event);
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
    const Bool found = originalCheckWindow(target, window, mask, event);
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
    const Bool found = originalCheckTyped(target, type, event);
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
    const Bool found = originalCheckTypedWindow(target, window, type, event);
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
        while (!originalCheckIf(target, event, predicate, arg))
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
    const Bool found = originalCheckIf(target, event, predicate, arg);
    if (found)
        recordEvent(*event, true);
    return found;
}

namespace lotus::apphook {

    int installX11Hooks() {
        if (!xlib)
            return 0;
        const std::array hooks{
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
