/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: MIT
 *
 */

/**
 * @file lotus-apphook-xcb.cpp
 * @brief Provides application-local keyboard events to native XCB consumers.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-target.h"

#include <xcb/xcb.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <linux/input-event-codes.h>
#include <mutex>
#include <sys/eventfd.h>
#include <time.h>
#include <poll.h>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

namespace lotus::apphook {
    namespace {

        struct XcbFocus {
            xcb_window_t          target            = XCB_NONE;
            xcb_window_t          focus             = XCB_NONE;
            xcb_window_t          root              = XCB_NONE;
            bool                  observedFocus     = false;
            bool                  focused           = false;
            bool                  keyboardAvailable = false;
            std::shared_ptr<Task> active;
            xcb_window_t          activeTarget = XCB_NONE;
            xcb_window_t          activeRoot   = XCB_NONE;
            size_t                opIndex      = 0;
            uint32_t              tapIndex     = 0;
            bool                  pressed      = false;
            int                   wakeFd       = -1;
            uint32_t              lastTime     = 0;
        };

        std::mutex                                      xcbMutex;
        std::unordered_map<xcb_connection_t*, XcbFocus> connections;
        std::unordered_set<xcb_connection_t*>           xlibOwned;
        std::unordered_set<xcb_connection_t*>           xcbEventOwners;
        std::mutex                                      resolveMutex;
        void*                                           xcbLibrary = nullptr;

        const xcb_setup_t* (*originalGetSetup)(xcb_connection_t*)                                                    = nullptr;
        xcb_screen_iterator_t (*originalRootsIterator)(const xcb_setup_t*)                                           = nullptr;
        int (*originalGetFd)(xcb_connection_t*)                                                                      = nullptr;
        int (*originalHasError)(xcb_connection_t*)                                                                   = nullptr;
        xcb_connection_t* (*originalConnect)(const char*, int*)                                                      = nullptr;
        xcb_connection_t* (*originalConnectFd)(int, xcb_auth_info_t*)                                                = nullptr;
        void (*originalDisconnect)(xcb_connection_t*)                                                                = nullptr;
        xcb_generic_event_t* (*originalPoll)(xcb_connection_t*)                                                      = nullptr;
        xcb_generic_event_t* (*originalPollQueued)(xcb_connection_t*)                                                = nullptr;
        xcb_generic_event_t* (*originalWait)(xcb_connection_t*)                                                      = nullptr;
        xcb_void_cookie_t (*originalChangeAttributes)(xcb_connection_t*, xcb_window_t, uint32_t, const void*)        = nullptr;
        xcb_void_cookie_t (*originalChangeAttributesChecked)(xcb_connection_t*, xcb_window_t, uint32_t, const void*) = nullptr;
        xcb_void_cookie_t (*originalCreateWindow)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_window_t, int16_t, int16_t, uint16_t, uint16_t, uint16_t, uint16_t, xcb_visualid_t,
                                                  uint32_t, const void*)                                             = nullptr;
        xcb_void_cookie_t (*originalCreateWindowChecked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_window_t, int16_t, int16_t, uint16_t, uint16_t, uint16_t, uint16_t,
                                                         xcb_visualid_t, uint32_t, const void*)                      = nullptr;

        template <typename Function>
        Function resolve(const char* symbol) {
            return reinterpret_cast<Function>(dlsym(xcbLibrary, symbol));
        }

        bool ownedByXlib(xcb_connection_t* connection) {
            return xlibOwned.contains(connection) && !xcbEventOwners.contains(connection);
        }

        xcb_window_t rootWindow(xcb_connection_t* connection) {
            const xcb_setup_t* setup = originalGetSetup ? originalGetSetup(connection) : nullptr;
            if (!setup)
                return XCB_NONE;
            const xcb_screen_iterator_t roots = originalRootsIterator ? originalRootsIterator(setup) : xcb_screen_iterator_t{};
            return roots.data ? roots.data->root : XCB_NONE;
        }

        void updateFocusState() {
            bool focused = false;
            bool ready   = false;
            for (const auto& [connection, state] : connections) {
                if (ownedByXlib(connection))
                    continue;
                focused |= state.focused;
                ready |= state.keyboardAvailable;
            }
            noteXcbKeyboardAvailable(ready);
            noteXcbFocus(focused);
        }

        void finishXcb(XcbFocus& state, int status);

        void observeNative(xcb_connection_t* connection, const xcb_generic_event_t* event) {
            if (!event)
                return;
            std::scoped_lock lock(xcbMutex);
            if (ownedByXlib(connection))
                return;
            auto&         state = connections[connection];
            const uint8_t type  = event->response_type & 0x7f;
            if (type == XCB_FOCUS_IN || type == XCB_FOCUS_OUT) {
                auto* focus = reinterpret_cast<const xcb_focus_in_event_t*>(event);
                if (state.active && (type == XCB_FOCUS_OUT || state.activeTarget != focus->event)) {
                    state.active->cancelled.store(true, std::memory_order_release);
                    if (!state.pressed)
                        finishXcb(state, -ECANCELED);
                }
                if (type == XCB_FOCUS_IN) {
                    state.target  = focus->event;
                    state.focus   = focus->event;
                    state.focused = true;
                } else if (!state.observedFocus || state.focus == focus->event) {
                    state.target  = XCB_NONE;
                    state.focus   = XCB_NONE;
                    state.focused = false;
                }
                state.observedFocus = true;
                updateFocusState();
            } else if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                const auto* key = reinterpret_cast<const xcb_key_press_event_t*>(event);
                // A key event can identify the target before the first FocusIn,
                // but must not resurrect a focus already revoked by FocusOut.
                if (!state.observedFocus || state.focused) {
                    state.target   = key->event;
                    state.root     = key->root;
                    state.lastTime = key->time;
                }
                state.keyboardAvailable = true;
                if (type == XCB_KEY_PRESS && !(event->response_type & 0x80) && (!state.observedFocus || state.focused)) {
                    state.focused = true;
                    updateFocusState();
                }
            }
        }

        bool subscribed(uint32_t mask, const void* values) {
            if (!(mask & XCB_CW_EVENT_MASK) || !values)
                return false;
            const auto*    items = static_cast<const uint32_t*>(values);
            const unsigned index = __builtin_popcount(mask & (XCB_CW_EVENT_MASK - 1));
            return (items[index] & (XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE)) != 0;
        }

        void subscribedKeyboard(xcb_connection_t* connection, uint32_t mask, const void* values) {
            if (!subscribed(mask, values))
                return;
            std::scoped_lock lock(xcbMutex);
            if (ownedByXlib(connection))
                return;
            connections[connection].keyboardAvailable = true;
            updateFocusState();
        }

        void finishXcb(XcbFocus& state, int status) {
            if (!state.active)
                return;
            {
                std::scoped_lock lock(runtime().mutex);
                finishLocked(state.active, status);
            }
            state.active.reset();
            state.activeTarget = XCB_NONE;
            state.activeRoot   = XCB_NONE;
            state.opIndex      = 0;
            state.tapIndex     = 0;
            state.pressed      = false;
        }

        unsigned modifierMask(uint32_t modifiers) {
            return ((modifiers & kModifierShift) ? XCB_MOD_MASK_SHIFT : 0) | ((modifiers & kModifierCtrl) ? XCB_MOD_MASK_CONTROL : 0) |
                ((modifiers & kModifierAlt) ? XCB_MOD_MASK_1 : 0) | ((modifiers & kModifierSuper) ? XCB_MOD_MASK_4 : 0);
        }

        xcb_generic_event_t* syntheticEdge(xcb_connection_t* connection, XcbFocus& state) {
            if (!state.active)
                return nullptr;
            auto& task = state.active;
            if (task->cancelled.load(std::memory_order_acquire) && !state.pressed) {
                finishXcb(state, -ECANCELED);
                return nullptr;
            }
            if (state.opIndex >= task->ops.size()) {
                finishXcb(state, 0);
                return nullptr;
            }
            auto* event = static_cast<xcb_key_press_event_t*>(std::calloc(1, sizeof(xcb_key_press_event_t)));
            if (!event) {
                finishXcb(state, -ENOMEM);
                return nullptr;
            }
            const KeyOperation& operation = task->ops[state.opIndex];
            event->response_type          = static_cast<uint8_t>((state.pressed ? XCB_KEY_RELEASE : XCB_KEY_PRESS) | 0x80);
            event->detail                 = static_cast<xcb_keycode_t>(operation.keycode + 8);
            event->event                  = state.activeTarget;
            if (!state.lastTime) {
                timespec now{};
                clock_gettime(CLOCK_MONOTONIC, &now);
                state.lastTime = static_cast<uint32_t>(now.tv_sec * 1000ULL + now.tv_nsec / 1000000);
            }
            event->time        = ++state.lastTime;
            event->root        = state.activeRoot != XCB_NONE ? state.activeRoot : rootWindow(connection);
            event->state       = static_cast<uint16_t>(modifierMask(operation.modifiers));
            event->same_screen = 1;
            if (!state.pressed) {
                task->applied.store(true, std::memory_order_release);
                state.pressed = true;
            } else {
                state.pressed = false;
                if (++state.tapIndex == operation.count) {
                    state.tapIndex = 0;
                    ++state.opIndex;
                }
                if (task->cancelled.load(std::memory_order_acquire))
                    finishXcb(state, -ECANCELED);
                else if (state.opIndex == task->ops.size())
                    finishXcb(state, 0);
            }
            return reinterpret_cast<xcb_generic_event_t*>(event);
        }

        xcb_generic_event_t* trySynthetic(xcb_connection_t* connection) {
            std::scoped_lock lock(xcbMutex);
            if (ownedByXlib(connection))
                return nullptr;
            auto& state = connections[connection];
            if (state.active) {
                if (!state.focused || !state.keyboardAvailable)
                    state.active->cancelled.store(true, std::memory_order_release);
                return syntheticEdge(connection, state);
            }
            if (originalHasError && originalHasError(connection))
                return nullptr;
            if (!state.focused || !state.keyboardAvailable || state.target == XCB_NONE)
                return nullptr;
            // There is one shared task queue, claimed only by a focused backend.
            state.active = takeTask();
            if (!state.active)
                return nullptr;
            state.activeTarget = state.target;
            state.activeRoot   = state.root;
            return syntheticEdge(connection, state);
        }

        bool needsKeyRelease(xcb_connection_t* connection) {
            std::scoped_lock lock(xcbMutex);
            const auto       it = connections.find(connection);
            return !ownedByXlib(connection) && it != connections.end() && it->second.active && it->second.pressed;
        }

        xcb_generic_event_t* checkedPoll(xcb_connection_t* connection, bool queuedOnly) {
            // Keep each press/release pair together; between taps, native events
            // (especially FocusOut) take priority over the remainder of a burst.
            if (needsKeyRelease(connection))
                return trySynthetic(connection);
            auto                 pollEvent = queuedOnly ? originalPollQueued : originalPoll;
            xcb_generic_event_t* event     = pollEvent ? pollEvent(connection) : nullptr;
            if (event) {
                observeNative(connection, event);
                return event;
            }
            if (queuedOnly) {
                pollfd fd{.fd = (originalGetFd ? originalGetFd(connection) : -1), .events = POLLIN, .revents = 0};
                if (poll(&fd, 1, 0) > 0)
                    return nullptr; // Unread server events are older than a synthetic burst.
            }
            return trySynthetic(connection);
        }

    } // namespace

    void loadXcb() {
        std::scoped_lock lock(resolveMutex);
        if (xcbLibrary)
            return;
        xcbLibrary = dlopen("libxcb.so.1", RTLD_NOW | RTLD_NOLOAD);
        if (!xcbLibrary)
            return;
        originalGetSetup                = resolve<decltype(originalGetSetup)>("xcb_get_setup");
        originalRootsIterator           = resolve<decltype(originalRootsIterator)>("xcb_setup_roots_iterator");
        originalGetFd                   = resolve<decltype(originalGetFd)>("xcb_get_file_descriptor");
        originalHasError                = resolve<decltype(originalHasError)>("xcb_connection_has_error");
        originalConnect                 = resolve<decltype(originalConnect)>("xcb_connect");
        originalConnectFd               = resolve<decltype(originalConnectFd)>("xcb_connect_to_fd");
        originalDisconnect              = resolve<decltype(originalDisconnect)>("xcb_disconnect");
        originalPoll                    = resolve<decltype(originalPoll)>("xcb_poll_for_event");
        originalPollQueued              = resolve<decltype(originalPollQueued)>("xcb_poll_for_queued_event");
        originalWait                    = resolve<decltype(originalWait)>("xcb_wait_for_event");
        originalChangeAttributes        = resolve<decltype(originalChangeAttributes)>("xcb_change_window_attributes");
        originalChangeAttributesChecked = resolve<decltype(originalChangeAttributesChecked)>("xcb_change_window_attributes_checked");
        originalCreateWindow            = resolve<decltype(originalCreateWindow)>("xcb_create_window");
        originalCreateWindowChecked     = resolve<decltype(originalCreateWindowChecked)>("xcb_create_window_checked");
    }

    void markXlibOwnedXcbConnection(void* ptr) {
        if (!ptr)
            return;
        auto*            connection = static_cast<xcb_connection_t*>(ptr);
        std::scoped_lock lock(xcbMutex);
        if (xlibOwned.insert(connection).second)
            updateFocusState();
    }

    void markXcbKeyboardSubscribed(void* ptr) {
        if (!ptr)
            return;
        auto*            connection = static_cast<xcb_connection_t*>(ptr);
        std::scoped_lock lock(xcbMutex);
        connections[connection].keyboardAvailable = true;
        updateFocusState();
    }

    void wakeXcb() {
        std::scoped_lock lock(xcbMutex);
        for (const auto& [connection, state] : connections) {
            if ((state.focused || state.active) && !ownedByXlib(connection) && state.wakeFd >= 0) {
                const uint64_t one = 1;
                (void)write(state.wakeFd, &one, sizeof(one));
            }
        }
    }

    void forgetXlibXcbConnection(void* ptr) {
        if (!ptr)
            return;
        auto*            connection = static_cast<xcb_connection_t*>(ptr);
        std::scoped_lock lock(xcbMutex);
        auto             it = connections.find(connection);
        if (it != connections.end()) {
            finishXcb(it->second, -ENOTCONN);
            if (it->second.wakeFd >= 0)
                close(it->second.wakeFd);
            connections.erase(it);
        }
        xlibOwned.erase(connection);
        xcbEventOwners.erase(connection);
        updateFocusState();
    }

    void setXcbEventQueueOwner(void* ptr, bool xcbOwns) {
        if (!ptr)
            return;
        auto*            connection = static_cast<xcb_connection_t*>(ptr);
        std::scoped_lock lock(xcbMutex);
        if (xcbOwns) {
            xcbEventOwners.insert(connection);
        } else {
            xcbEventOwners.erase(connection);
            auto it = connections.find(connection);
            if (it != connections.end())
                finishXcb(it->second, -ECANCELED);
        }
        updateFocusState();
    }

    bool xcbOwnsXlibConnection(void* ptr) {
        std::scoped_lock lock(xcbMutex);
        return xcbEventOwners.contains(static_cast<xcb_connection_t*>(ptr));
    }

} // namespace lotus::apphook

#define LH_EXPORT extern "C" __attribute__((visibility("default")))

LH_EXPORT xcb_connection_t* xcb_connect(const char* display, int* screen) {
    using namespace lotus::apphook;
    if (!originalConnect)
        loadXcb();
    return originalConnect ? originalConnect(display, screen) : nullptr;
}

LH_EXPORT xcb_connection_t* xcb_connect_to_fd(int fd, xcb_auth_info_t* auth) {
    using namespace lotus::apphook;
    if (!originalConnectFd)
        loadXcb();
    return originalConnectFd ? originalConnectFd(fd, auth) : nullptr;
}

LH_EXPORT void xcb_disconnect(xcb_connection_t* connection) {
    using namespace lotus::apphook;
    if (!originalDisconnect)
        loadXcb();
    {
        std::scoped_lock lock(xcbMutex);
        auto             it = connections.find(connection);
        if (it != connections.end()) {
            finishXcb(it->second, -ENOTCONN);
            if (it->second.wakeFd >= 0)
                close(it->second.wakeFd);
            connections.erase(it);
        }
        xlibOwned.erase(connection);
        xcbEventOwners.erase(connection);
        updateFocusState();
    }
    if (originalDisconnect)
        originalDisconnect(connection);
}

LH_EXPORT xcb_generic_event_t* xcb_poll_for_event(xcb_connection_t* connection) {
    using namespace lotus::apphook;
    if (!originalPoll)
        loadXcb();
    return originalPoll ? checkedPoll(connection, false) : nullptr;
}

LH_EXPORT xcb_generic_event_t* xcb_poll_for_queued_event(xcb_connection_t* connection) {
    using namespace lotus::apphook;
    if (!originalPollQueued)
        loadXcb();
    return originalPollQueued ? checkedPoll(connection, true) : nullptr;
}

LH_EXPORT xcb_generic_event_t* xcb_wait_for_event(xcb_connection_t* connection) {
    using namespace lotus::apphook;
    if (!originalWait)
        loadXcb();
    if (!originalWait)
        return nullptr;
    int wake = -1;
    {
        std::scoped_lock lock(xcbMutex);
        auto&            state = connections[connection];
        if (state.wakeFd < 0)
            state.wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        wake = state.wakeFd;
    }
    if (wake < 0 || !originalPoll) {
        auto* event = originalWait(connection);
        observeNative(connection, event);
        return event;
    }
    while (true) {
        if (auto* event = checkedPoll(connection, false))
            return event;
        if (originalHasError && originalHasError(connection))
            return nullptr;
        pollfd fds[2]{{.fd = (originalGetFd ? originalGetFd(connection) : -1), .events = POLLIN, .revents = 0}, {.fd = wake, .events = POLLIN, .revents = 0}};
        int    ready;
        do {
            ready = poll(fds, 2, -1);
        } while (ready < 0 && errno == EINTR);
        if (ready < 0)
            return nullptr;
        if (fds[1].revents & POLLIN) {
            uint64_t value;
            while (read(wake, &value, sizeof(value)) == sizeof(value)) {}
        }
    }
}

LH_EXPORT xcb_void_cookie_t xcb_change_window_attributes(xcb_connection_t* connection, xcb_window_t window, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    if (!originalChangeAttributes)
        loadXcb();
    const auto result = originalChangeAttributes ? originalChangeAttributes(connection, window, mask, values) : xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, mask, values);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_change_window_attributes_checked(xcb_connection_t* connection, xcb_window_t window, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    if (!originalChangeAttributesChecked)
        loadXcb();
    const auto result = originalChangeAttributesChecked ? originalChangeAttributesChecked(connection, window, mask, values) : xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, mask, values);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_create_window(xcb_connection_t* connection, uint8_t depth, xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                              uint16_t height, uint16_t border, uint16_t windowClass, xcb_visualid_t visual, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    if (!originalCreateWindow)
        loadXcb();
    const auto result =
        originalCreateWindow ? originalCreateWindow(connection, depth, window, parent, x, y, width, height, border, windowClass, visual, mask, values) : xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, mask, values);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_create_window_checked(xcb_connection_t* connection, uint8_t depth, xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                                      uint16_t height, uint16_t border, uint16_t windowClass, xcb_visualid_t visual, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    if (!originalCreateWindowChecked)
        loadXcb();
    const auto result = originalCreateWindowChecked ?
        originalCreateWindowChecked(connection, depth, window, parent, x, y, width, height, border, windowClass, visual, mask, values) :
        xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, mask, values);
    return result;
}

namespace lotus::apphook {
    int installXcbHooks() {
        if (!xcbLibrary)
            return 0;
        const std::array hooks{
            HookSpec{"xcb_connect", reinterpret_cast<void*>(&xcb_connect)},
            HookSpec{"xcb_connect_to_fd", reinterpret_cast<void*>(&xcb_connect_to_fd)},
            HookSpec{"xcb_disconnect", reinterpret_cast<void*>(&xcb_disconnect)},
            HookSpec{"xcb_poll_for_event", reinterpret_cast<void*>(&xcb_poll_for_event)},
            HookSpec{"xcb_poll_for_queued_event", reinterpret_cast<void*>(&xcb_poll_for_queued_event)},
            HookSpec{"xcb_wait_for_event", reinterpret_cast<void*>(&xcb_wait_for_event)},
            HookSpec{"xcb_change_window_attributes", reinterpret_cast<void*>(&xcb_change_window_attributes)},
            HookSpec{"xcb_change_window_attributes_checked", reinterpret_cast<void*>(&xcb_change_window_attributes_checked)},
            HookSpec{"xcb_create_window", reinterpret_cast<void*>(&xcb_create_window)},
            HookSpec{"xcb_create_window_checked", reinterpret_cast<void*>(&xcb_create_window_checked)},
        };
        return installHooks(hooks);
    }
} // namespace lotus::apphook
