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
#include "lotus-apphook-sequence.h"
#include "lotus-apphook-xfocus.h"

#include <xcb/xcb.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <linux/input-event-codes.h>
#include <mutex>
#include <time.h>
#include <poll.h>
#include <unordered_map>
#include <unordered_set>

namespace lotus::apphook {
    namespace {

        struct XcbFocus : XFocusState<xcb_window_t, XCB_NONE> {
            xcb_window_t          root              = XCB_NONE;
            xcb_window_t          wakeWindow        = XCB_NONE;
            bool                  keyboardAvailable = false;
            std::shared_ptr<Task> active;
            xcb_window_t          activeTarget = XCB_NONE;
            xcb_window_t          activeRoot   = XCB_NONE;
            KeySequence           sequence;
            uint32_t              lastTime        = 0;
            unsigned              blockingWaiters = 0;
        };

        std::mutex                                      xcbMutex;
        std::unordered_map<xcb_connection_t*, XcbFocus> connections;
        std::unordered_set<xcb_connection_t*>           xlibOwned;
        std::unordered_set<xcb_connection_t*>           xcbEventOwners;
        // A self-directed ClientMessage wakes libxcb's native event waiters.
        // Never bypass libxcb's internal event queue synchronization with poll().
        constexpr char kWakePayload[] = "lotus-apphook-wakeup";
        static_assert(sizeof(kWakePayload) - 1 == sizeof(xcb_client_message_data_t::data8));
        std::mutex resolveMutex;
        void*      xcbLibrary = nullptr;

        const xcb_setup_t* (*originalGetSetup)(xcb_connection_t*)                                                    = nullptr;
        xcb_screen_iterator_t (*originalRootsIterator)(const xcb_setup_t*)                                           = nullptr;
        int (*originalGetFd)(xcb_connection_t*)                                                                      = nullptr;
        int (*originalHasError)(xcb_connection_t*)                                                                   = nullptr;
        xcb_connection_t* (*originalConnect)(const char*, int*)                                                      = nullptr;
        xcb_connection_t* (*originalConnectFd)(int, xcb_auth_info_t*)                                                = nullptr;
        uint32_t (*originalGenerateId)(xcb_connection_t*)                                                            = nullptr;
        void (*originalDisconnect)(xcb_connection_t*)                                                                = nullptr;
        xcb_generic_event_t* (*originalPoll)(xcb_connection_t*)                                                      = nullptr;
        xcb_generic_event_t* (*originalPollQueued)(xcb_connection_t*)                                                = nullptr;
        xcb_generic_event_t* (*originalWait)(xcb_connection_t*)                                                      = nullptr;
        xcb_void_cookie_t (*originalSendEvent)(xcb_connection_t*, uint8_t, xcb_window_t, uint32_t, const char*)      = nullptr;
        int (*originalFlush)(xcb_connection_t*)                                                                      = nullptr;
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
                    if (!state.sequence.needsRelease())
                        finishXcb(state, -ECANCELED);
                }
                if (type == XCB_FOCUS_IN)
                    state.enter(focus->event);
                else
                    state.leave(focus->event, true);
                updateFocusState();
            } else if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                const auto* key = reinterpret_cast<const xcb_key_press_event_t*>(event);
                // A key event can identify the target before the first FocusIn,
                // but must not resurrect a focus already revoked by FocusOut.
                if (state.acceptsKey()) {
                    state.keyTarget(key->event);
                    state.root     = key->root;
                    state.lastTime = key->time;
                }
                state.keyboardAvailable = true;
                if (type == XCB_KEY_PRESS && !(event->response_type & 0x80) && state.acceptsKey()) {
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
            state.sequence.reset();
        }

        unsigned modifierMask(uint32_t modifiers) {
            return ((modifiers & kModifierShift) ? XCB_MOD_MASK_SHIFT : 0) | ((modifiers & kModifierCtrl) ? XCB_MOD_MASK_CONTROL : 0) |
                ((modifiers & kModifierAlt) ? XCB_MOD_MASK_1 : 0) | ((modifiers & kModifierSuper) ? XCB_MOD_MASK_4 : 0);
        }

        xcb_generic_event_t* syntheticEdge(xcb_connection_t* connection, XcbFocus& state) {
            if (!state.active)
                return nullptr;
            auto& task = state.active;
            if (task->cancelled.load(std::memory_order_acquire) && !state.sequence.needsRelease()) {
                finishXcb(state, -ECANCELED);
                return nullptr;
            }
            const auto edge = state.sequence.current();
            if (!edge) {
                finishXcb(state, 0);
                return nullptr;
            }
            auto* event = static_cast<xcb_key_press_event_t*>(std::calloc(1, std::max(sizeof(xcb_key_press_event_t), sizeof(xcb_generic_event_t))));
            if (!event) {
                finishXcb(state, -ENOMEM);
                return nullptr;
            }
            event->response_type = static_cast<uint8_t>((edge->pressed ? XCB_KEY_PRESS : XCB_KEY_RELEASE) | 0x80);
            event->detail        = static_cast<xcb_keycode_t>(edge->keycode + 8);
            event->event         = state.activeTarget;
            if (!state.lastTime) {
                timespec now{};
                clock_gettime(CLOCK_MONOTONIC, &now);
                state.lastTime = static_cast<uint32_t>(now.tv_sec * 1000ULL + now.tv_nsec / 1000000);
            }
            event->time        = ++state.lastTime;
            event->root        = state.activeRoot != XCB_NONE ? state.activeRoot : rootWindow(connection);
            event->state       = static_cast<uint16_t>(modifierMask(edge->modifiers));
            event->same_screen = 1;
            if (edge->pressed)
                task->applied.store(true, std::memory_order_release);
            state.sequence.advance();
            if (!edge->pressed) {
                if (task->cancelled.load(std::memory_order_acquire))
                    finishXcb(state, -ECANCELED);
                else if (state.sequence.completed())
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
            state.sequence.reset(state.active->ops);
            return syntheticEdge(connection, state);
        }

        bool needsKeyRelease(xcb_connection_t* connection) {
            std::scoped_lock lock(xcbMutex);
            const auto       it = connections.find(connection);
            return !ownedByXlib(connection) && it != connections.end() && it->second.active && it->second.sequence.needsRelease();
        }

        bool isWakeEvent(xcb_connection_t* connection, const xcb_generic_event_t* event) {
            if (!event || (event->response_type & 0x7f) != XCB_CLIENT_MESSAGE || !(event->response_type & 0x80))
                return false;
            const auto* message = reinterpret_cast<const xcb_client_message_event_t*>(event);
            if (message->format != 8 || message->type != XCB_ATOM_NONE || std::memcmp(message->data.data8, kWakePayload, 20) != 0)
                return false;
            std::scoped_lock lock(xcbMutex);
            const auto       it = connections.find(connection);
            return it != connections.end() && it->second.wakeWindow == message->window;
        }

        void ensureWakeWindow(xcb_connection_t* connection, XcbFocus& state) {
            if (state.wakeWindow != XCB_NONE || !originalGenerateId || !originalCreateWindow)
                return;
            const xcb_window_t root = rootWindow(connection);
            if (root == XCB_NONE)
                return;
            const xcb_window_t window = originalGenerateId(connection);
            if (window == XCB_NONE)
                return;
            // An unmapped, connection-owned InputOnly window receives the wake
            // ClientMessage even when the focused application window is replaced.
            const auto cookie = originalCreateWindow(connection, XCB_COPY_FROM_PARENT, window, root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0, nullptr);
            if (cookie.sequence)
                state.wakeWindow = window;
        }

        xcb_generic_event_t* checkedPoll(xcb_connection_t* connection, bool queuedOnly) {
            // Keep each press/release pair together; between taps, native events
            // (especially FocusOut) take priority over the remainder of a burst.
            if (needsKeyRelease(connection))
                return trySynthetic(connection);
            const auto pollEvent = queuedOnly ? originalPollQueued : originalPoll;
            while (xcb_generic_event_t* event = pollEvent ? pollEvent(connection) : nullptr) {
                if (isWakeEvent(connection, event)) {
                    std::free(event);
                    // A wake is ordered behind all earlier native events on the server.
                    // Claim the pending task here even if another thread is waiting.
                    if (auto* synthetic = trySynthetic(connection))
                        return synthetic;
                    continue;
                }
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
        originalGenerateId              = resolve<decltype(originalGenerateId)>("xcb_generate_id");
        originalDisconnect              = resolve<decltype(originalDisconnect)>("xcb_disconnect");
        originalPoll                    = resolve<decltype(originalPoll)>("xcb_poll_for_event");
        originalPollQueued              = resolve<decltype(originalPollQueued)>("xcb_poll_for_queued_event");
        originalWait                    = resolve<decltype(originalWait)>("xcb_wait_for_event");
        originalSendEvent               = resolve<decltype(originalSendEvent)>("xcb_send_event");
        originalFlush                   = resolve<decltype(originalFlush)>("xcb_flush");
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
        if (!originalSendEvent || !originalFlush)
            return;
        for (const auto& [connection, state] : connections) {
            if (!(state.focused || state.active) || ownedByXlib(connection) || !state.blockingWaiters || state.wakeWindow == XCB_NONE)
                continue;
            xcb_client_message_event_t event{};
            event.response_type = XCB_CLIENT_MESSAGE;
            event.format        = 8;
            event.window        = state.wakeWindow;
            event.type          = XCB_ATOM_NONE;
            std::memcpy(event.data.data8, kWakePayload, 20);
            // Wake each native libxcb waiter, without using its socket fd as an
            // external event queue (which would miss internally queued events).
            bool sent = false;
            for (unsigned i = 0; i < state.blockingWaiters; ++i)
                sent |= originalSendEvent(connection, 0, state.wakeWindow, 0, reinterpret_cast<const char*>(&event)).sequence != 0;
            if (sent)
                (void)originalFlush(connection);
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

namespace lotus::apphook {
    void prepareXcbFork() {
        xcbMutex.lock();
    }
    void finishXcbFork(bool child) {
        if (child) {
            connections.clear(); // closes inherited per-connection eventfds
            xlibOwned.clear();
            xcbEventOwners.clear();
        }
        xcbMutex.unlock();
    }
}

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
    while (true) {
        if (auto* event = checkedPoll(connection, false))
            return event;
        // Use libxcb's own blocking wait: other threads may have consumed socket
        // bytes while queuing events internally, so polling the raw fd can hang.
        {
            std::scoped_lock lock(xcbMutex);
            auto&            state = connections[connection];
            if ((state.focused && state.keyboardAvailable) || state.active)
                ensureWakeWindow(connection, state);
            ++state.blockingWaiters;
        }
        // Recheck after registering as a waiter: a task may have arrived
        // between the first check and wakeup registration.
        if (auto* event = checkedPoll(connection, false)) {
            std::scoped_lock lock(xcbMutex);
            auto             it = connections.find(connection);
            if (it != connections.end() && it->second.blockingWaiters)
                --it->second.blockingWaiters;
            return event;
        }
        auto* event = originalWait(connection);
        {
            std::scoped_lock lock(xcbMutex);
            auto             it = connections.find(connection);
            if (it != connections.end() && it->second.blockingWaiters)
                --it->second.blockingWaiters;
        }
        if (!event)
            return nullptr;
        if (isWakeEvent(connection, event)) {
            std::free(event);
            if (auto* synthetic = trySynthetic(connection))
                return synthetic;
            continue;
        }
        observeNative(connection, event);
        return event;
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
