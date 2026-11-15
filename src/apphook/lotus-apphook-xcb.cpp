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
            xcb_window_t                               root                 = XCB_NONE;
            xcb_window_t                               wakeWindow           = XCB_NONE;
            bool                                       keyboardAvailable    = false;
            bool                                       nativeKeyboard       = false;
            xcb_window_t                               nativeKeyboardWindow = XCB_NONE;
            std::unordered_map<xcb_window_t, uint32_t> keyboardMasks;
            int                                        screenIndex = -1; ///< Unknown for xcb_connect_to_fd
            std::shared_ptr<Task>                      active;
            xcb_window_t                               activeTarget = XCB_NONE;
            xcb_window_t                               activeRoot   = XCB_NONE;
            KeySequence                                sequence;
            uint32_t                                   lastTime        = 0;
            unsigned                                   blockingWaiters = 0;
        };

        std::mutex xcbMutex;
        using ConnectionMap = std::unordered_map<xcb_connection_t*, XcbFocus>;
        using ConnectionSet = std::unordered_set<xcb_connection_t*>;
        // Fork cleanup reads these without invoking any lazy storage accessor.
        std::atomic<ConnectionMap*> initializedConnections{nullptr};
        std::atomic<ConnectionSet*> initializedXlibOwned{nullptr};
        std::atomic<ConnectionSet*> initializedXcbEventOwners{nullptr};
        ConnectionMap&              connectionsStorage() {
            static ConnectionMap state;
            initializedConnections.store(&state, std::memory_order_release);
            return state;
        }
#define connections (connectionsStorage())
        ConnectionSet& xlibOwnedStorage() {
            static ConnectionSet state;
            initializedXlibOwned.store(&state, std::memory_order_release);
            return state;
        }
#define xlibOwned (xlibOwnedStorage())
        ConnectionSet& xcbEventOwnersStorage() {
            static ConnectionSet state;
            initializedXcbEventOwners.store(&state, std::memory_order_release);
            return state;
        }
#define xcbEventOwners (xcbEventOwnersStorage())
        // A self-directed ClientMessage wakes libxcb's native event waiters.
        // Never bypass libxcb's internal event queue synchronization with poll().
        constexpr char kWakePayload[] = "lotus-apphook-wakeup";
        static_assert(sizeof(kWakePayload) - 1 == sizeof(xcb_client_message_data_t::data8));
        std::once_flag resolveOnce;
        void*          xcbLibrary = nullptr;

        const xcb_setup_t* (*originalGetSetup)(xcb_connection_t*)                                                    = nullptr;
        xcb_screen_iterator_t (*originalRootsIterator)(const xcb_setup_t*)                                           = nullptr;
        void (*originalScreenNext)(xcb_screen_iterator_t*)                                                           = nullptr;
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
        xcb_void_cookie_t (*originalDestroyWindow)(xcb_connection_t*, xcb_window_t)                                  = nullptr;
        xcb_void_cookie_t (*originalDestroyWindowChecked)(xcb_connection_t*, xcb_window_t)                           = nullptr;
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
            auto       roots = originalRootsIterator ? originalRootsIterator(setup) : xcb_screen_iterator_t{};
            const auto it    = connections.find(connection);
            const int  index = it != connections.end() ? it->second.screenIndex : 0;
            if (index < 0) {
                // xcb_connect_to_fd has no screen argument. A first-screen guess
                // is unsafe on a multi-screen server before a native key event.
                if (!roots.data || roots.rem != 1)
                    return XCB_NONE;
            }
            if (index > 0 && !originalScreenNext)
                return XCB_NONE;
            for (int i = 0; roots.rem && i < index && originalScreenNext; ++i)
                originalScreenNext(&roots);
            return roots.data && roots.rem ? roots.data->root : XCB_NONE;
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

        void forgetWindow(xcb_connection_t* connection, xcb_window_t window) {
            std::scoped_lock lock(xcbMutex);
            auto             it = connections.find(connection);
            if (it == connections.end())
                return;
            auto& state = it->second;
            state.keyboardMasks.erase(window);
            if (state.nativeKeyboardWindow == window) {
                state.nativeKeyboardWindow = XCB_NONE;
                state.nativeKeyboard       = false;
            }
            if (state.target == window)
                state.target = XCB_NONE;
            if (state.focus == window)
                state.leave(window);
            if (state.active && state.activeTarget == window) {
                state.active->cancelled.store(true, std::memory_order_release);
                if (!state.sequence.needsRelease())
                    finishXcb(state, -ECANCELED);
            }
            state.keyboardAvailable = state.nativeKeyboard || !state.keyboardMasks.empty();
            updateFocusState();
        }

        void observeNative(xcb_connection_t* connection, const xcb_generic_event_t* event) {
            if (!event)
                return;
            std::scoped_lock lock(xcbMutex);
            if (ownedByXlib(connection))
                return;
            auto&         state = connections[connection];
            const uint8_t type  = event->response_type & 0x7f;
            if (type == XCB_FOCUS_IN || type == XCB_FOCUS_OUT) {
                const auto*        focus         = reinterpret_cast<const xcb_focus_in_event_t*>(event);
                const bool         wasFocused    = state.focused;
                const xcb_window_t previousOwner = state.focus;
                if (type == XCB_FOCUS_IN)
                    state.enter(focus->event);
                else
                    state.leave(focus->event);
                const bool lostFocus    = wasFocused && !state.focused;
                const bool changedOwner = type == XCB_FOCUS_IN && wasFocused && previousOwner != focus->event;
                if (state.active && (lostFocus || changedOwner)) {
                    state.active->cancelled.store(true, std::memory_order_release);
                    if (!state.sequence.needsRelease())
                        finishXcb(state, -ECANCELED);
                }
                updateFocusState();
            } else if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                const auto* key = reinterpret_cast<const xcb_key_press_event_t*>(event);
                // A key event can identify the target before the first FocusIn,
                // but must not resurrect a focus already revoked by FocusOut.
                if (state.acceptsKey()) {
                    state.keyTarget(key->event);
                    if (!state.observedFocus && state.focus == XCB_NONE)
                        state.focus = key->event;
                    state.root     = key->root;
                    state.lastTime = key->time;
                }
                state.nativeKeyboard       = true;
                state.nativeKeyboardWindow = key->event;
                state.keyboardAvailable    = true;
                if (type == XCB_KEY_PRESS && !(event->response_type & 0x80) && state.acceptsKey()) {
                    state.focused = true;
                    updateFocusState();
                }
            }
        }

        /** @brief Extracts the event mask from XCB packed window attributes. */
        bool keyboardMask(uint32_t mask, const void* values, uint32_t& result) {
            if (!(mask & XCB_CW_EVENT_MASK) || !values)
                return false;
            const auto*    items = static_cast<const uint32_t*>(values);
            const unsigned index = __builtin_popcount(mask & (XCB_CW_EVENT_MASK - 1));
            result               = items[index] & (XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE);
            return true;
        }

        void subscribedKeyboard(xcb_connection_t* connection, xcb_window_t window, uint32_t mask, const void* values) {
            uint32_t keys = 0;
            if (!keyboardMask(mask, values, keys))
                return;
            std::scoped_lock lock(xcbMutex);
            if (ownedByXlib(connection))
                return;
            auto& state = connections[connection];
            if (keys)
                state.keyboardMasks[window] = keys;
            else {
                state.keyboardMasks.erase(window);
                if (state.nativeKeyboardWindow == window) {
                    state.nativeKeyboardWindow = XCB_NONE;
                    state.nativeKeyboard       = false;
                }
            }
            state.keyboardAvailable = state.nativeKeyboard || !state.keyboardMasks.empty();
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
            state.active = takeTask(HookBackend::Xcb, connection, static_cast<uintptr_t>(state.target));
            if (!state.active)
                return nullptr;
            // X11 core keycodes occupy 8..255; reject rather than truncate.
            for (const auto& operation : state.active->ops) {
                if (operation.keycode > 247) {
                    finishXcb(state, -EINVAL);
                    return nullptr;
                }
            }
            // Do not claim a task until its root can be resolved safely.
            const xcb_window_t root = state.root != XCB_NONE ? state.root : rootWindow(connection);
            if (root == XCB_NONE) {
                // Put it back at the head for a later native keyboard event.
                // Still under xcbMutex; takeTask() itself only holds runtime.mutex.
                {
                    std::scoped_lock pendingLock(runtime().mutex);
                    runtime().pending.push_front(state.active);
                }
                state.active.reset();
                return nullptr;
            }
            state.activeTarget = state.target;
            state.activeRoot   = root;
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
                // xcb_poll_for_queued_event must not read from the connection.
                // When socket data is waiting, it might contain an older native
                // event; postpone the synthetic burst rather than violate the
                // queued-only contract by calling xcb_poll_for_event.
                pollfd fd{.fd = (originalGetFd ? originalGetFd(connection) : -1), .events = POLLIN, .revents = 0};
                if (fd.fd >= 0 && poll(&fd, 1, 0) != 0)
                    return nullptr;
            }
            return trySynthetic(connection);
        }

    } // namespace

    FocusCandidates focusedXcbCandidates() {
        std::scoped_lock lock(xcbMutex);
        FocusCandidates  candidates;
        for (const auto& [connection, state] : connections) {
            if (ownedByXlib(connection) || !state.focused || !state.keyboardAvailable || state.target == XCB_NONE)
                continue;
            ++candidates.count;
            candidates.target = {HookBackend::Xcb, connection, static_cast<uintptr_t>(state.target)};
        }
        return candidates;
    }

    void loadXcb() {
        ensureRuntimeInitialized();
        std::call_once(resolveOnce, [] {
            xcbLibrary = dlopen("libxcb.so.1", RTLD_NOW | RTLD_NOLOAD);
            if (!xcbLibrary)
                return;
            originalGetSetup                = resolve<decltype(originalGetSetup)>("xcb_get_setup");
            originalRootsIterator           = resolve<decltype(originalRootsIterator)>("xcb_setup_roots_iterator");
            originalScreenNext              = resolve<decltype(originalScreenNext)>("xcb_screen_next");
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
            originalDestroyWindow           = resolve<decltype(originalDestroyWindow)>("xcb_destroy_window");
            originalDestroyWindowChecked    = resolve<decltype(originalDestroyWindowChecked)>("xcb_destroy_window_checked");
            originalChangeAttributes        = resolve<decltype(originalChangeAttributes)>("xcb_change_window_attributes");
            originalChangeAttributesChecked = resolve<decltype(originalChangeAttributesChecked)>("xcb_change_window_attributes_checked");
            originalCreateWindow            = resolve<decltype(originalCreateWindow)>("xcb_create_window");
            originalCreateWindowChecked     = resolve<decltype(originalCreateWindowChecked)>("xcb_create_window_checked");
        });
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

    void setXcbEventQueueOwner(void* ptr, bool xcbOwns, uint32_t root) {
        if (!ptr)
            return;
        auto*            connection = static_cast<xcb_connection_t*>(ptr);
        std::scoped_lock lock(xcbMutex);
        if (xcbOwns) {
            xcbEventOwners.insert(connection);
            if (root != XCB_NONE)
                connections[connection].root = root;
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
            if (auto* state = initializedConnections.load(std::memory_order_acquire))
                state->clear(); // closes inherited per-connection eventfds
            if (auto* state = initializedXlibOwned.load(std::memory_order_acquire))
                state->clear();
            if (auto* state = initializedXcbEventOwners.load(std::memory_order_acquire))
                state->clear();
        }
        xcbMutex.unlock();
    }
}

// The preloaded C shim is the only externally visible interposer.
#define xcb_connect                          lotus_apphook_impl_xcb_connect
#define xcb_connect_to_fd                    lotus_apphook_impl_xcb_connect_to_fd
#define xcb_destroy_window                   lotus_apphook_impl_xcb_destroy_window
#define xcb_destroy_window_checked           lotus_apphook_impl_xcb_destroy_window_checked
#define xcb_disconnect                       lotus_apphook_impl_xcb_disconnect
#define xcb_poll_for_event                   lotus_apphook_impl_xcb_poll_for_event
#define xcb_poll_for_queued_event            lotus_apphook_impl_xcb_poll_for_queued_event
#define xcb_wait_for_event                   lotus_apphook_impl_xcb_wait_for_event
#define xcb_change_window_attributes         lotus_apphook_impl_xcb_change_window_attributes
#define xcb_change_window_attributes_checked lotus_apphook_impl_xcb_change_window_attributes_checked
#define xcb_create_window                    lotus_apphook_impl_xcb_create_window
#define xcb_create_window_checked            lotus_apphook_impl_xcb_create_window_checked
#define LH_EXPORT                            extern "C" __attribute__((visibility("hidden")))

LH_EXPORT void lotus_apphook_adopt_xcb_connection(xcb_connection_t* connection, int screen) {
    using namespace lotus::apphook;
    loadXcb();
    if (!connection)
        return;
    std::scoped_lock lock(xcbMutex);
    connections[connection].screenIndex = screen;
}

LH_EXPORT xcb_connection_t* xcb_connect(const char* display, int* screen) {
    using namespace lotus::apphook;
    loadXcb();
    int               selected   = 0;
    int*              screenOut  = screen ? screen : &selected;
    xcb_connection_t* connection = originalConnect ? originalConnect(display, screenOut) : nullptr;
    if (connection) {
        std::scoped_lock lock(xcbMutex);
        connections[connection].screenIndex = *screenOut;
    }
    return connection;
}

LH_EXPORT xcb_connection_t* xcb_connect_to_fd(int fd, xcb_auth_info_t* auth) {
    using namespace lotus::apphook;
    loadXcb();
    auto* connection = originalConnectFd ? originalConnectFd(fd, auth) : nullptr;
    if (connection) {
        std::scoped_lock lock(xcbMutex);
        connections[connection].screenIndex = -1;
    }
    return connection;
}

LH_EXPORT xcb_void_cookie_t xcb_destroy_window(xcb_connection_t* connection, xcb_window_t window) {
    using namespace lotus::apphook;
    loadXcb();
    const auto result = originalDestroyWindow ? originalDestroyWindow(connection, window) : xcb_void_cookie_t{};
    if (result.sequence)
        forgetWindow(connection, window);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_destroy_window_checked(xcb_connection_t* connection, xcb_window_t window) {
    using namespace lotus::apphook;
    loadXcb();
    const auto result = originalDestroyWindowChecked ? originalDestroyWindowChecked(connection, window) : xcb_void_cookie_t{};
    if (result.sequence)
        forgetWindow(connection, window);
    return result;
}

LH_EXPORT void xcb_disconnect(xcb_connection_t* connection) {
    using namespace lotus::apphook;
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
    loadXcb();
    return originalPoll ? checkedPoll(connection, false) : nullptr;
}

LH_EXPORT xcb_generic_event_t* xcb_poll_for_queued_event(xcb_connection_t* connection) {
    using namespace lotus::apphook;
    loadXcb();
    return originalPollQueued ? checkedPoll(connection, true) : nullptr;
}

LH_EXPORT xcb_generic_event_t* xcb_wait_for_event(xcb_connection_t* connection) {
    using namespace lotus::apphook;
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
    loadXcb();
    const auto result = originalChangeAttributes ? originalChangeAttributes(connection, window, mask, values) : xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, window, mask, values);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_change_window_attributes_checked(xcb_connection_t* connection, xcb_window_t window, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    loadXcb();
    const auto result = originalChangeAttributesChecked ? originalChangeAttributesChecked(connection, window, mask, values) : xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, window, mask, values);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_create_window(xcb_connection_t* connection, uint8_t depth, xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                              uint16_t height, uint16_t border, uint16_t windowClass, xcb_visualid_t visual, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    loadXcb();
    const auto result =
        originalCreateWindow ? originalCreateWindow(connection, depth, window, parent, x, y, width, height, border, windowClass, visual, mask, values) : xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, window, mask, values);
    return result;
}

LH_EXPORT xcb_void_cookie_t xcb_create_window_checked(xcb_connection_t* connection, uint8_t depth, xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                                      uint16_t height, uint16_t border, uint16_t windowClass, xcb_visualid_t visual, uint32_t mask, const void* values) {
    using namespace lotus::apphook;
    loadXcb();
    const auto result = originalCreateWindowChecked ?
        originalCreateWindowChecked(connection, depth, window, parent, x, y, width, height, border, windowClass, visual, mask, values) :
        xcb_void_cookie_t{};
    if (result.sequence)
        subscribedKeyboard(connection, window, mask, values);
    return result;
}
