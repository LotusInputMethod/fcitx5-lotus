/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-target.h
 * @brief Defines the injected hook runtime and backend interface.
 */

#ifndef _SRC_LOTUS_APPHOOK_TARGET_H_
#define _SRC_LOTUS_APPHOOK_TARGET_H_

#include "lotus-apphook-protocol.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <vector>

struct wl_display;

namespace lotus::apphook {

    enum class HookBackend : uint8_t {
        None,
        X11,
        Xcb,
        Wayland
    };

    /** A focused frontend and its input target, captured when Emit arrives. */
    struct HookTarget {
        HookBackend backend    = HookBackend::None;
        const void* connection = nullptr;
        uintptr_t   focused    = 0;
    };

    /** One frontend's focus snapshot; count > 1 marks ambiguous focus. */
    struct FocusCandidates {
        HookTarget target{};
        unsigned   count = 0;
    };

    /**
     * @brief An in-flight keyboard request and its completion state.
     */
    struct Task {
        uint64_t                  requestId = 0;
        HookTarget                target{};
        std::vector<KeyOperation> ops;
        size_t                    x11Remaining = 0;
        int                       status       = 0;
        bool                      done         = false;
        std::atomic<bool>         cancelled{false};
        std::atomic<bool>         applied{false};
    };

    /**
     * @brief Shared request queue and input backend state in the target process.
     */
    struct Runtime {
        Runtime();
        ~Runtime();
        std::atomic<int>                  ownerPid;
        std::mutex                        mutex;
        std::mutex                        workerMutex;
        pthread_t                         worker{};
        bool                              workerJoinable = false;
        std::atomic<bool>                 workersStarted{false};
        std::atomic<bool>                 initialized{false};
        std::atomic<bool>                 focusedX11{false};
        std::atomic<bool>                 observedX11Focus{false};
        std::atomic<bool>                 focusedWayland{false};
        std::atomic<bool>                 focusedXcb{false};
        std::deque<std::shared_ptr<Task>> pending;
        std::shared_ptr<Task>             activeX11;
        void*                             activeX11Display = nullptr;
        size_t                            inFlight         = 0;
        std::atomic<int>                  completionFd{-1};
        std::atomic<int>                  connectionFd{-1};
        std::atomic<bool>                 readyX11{false};
        std::atomic<bool>                 readyWayland{false};
        std::atomic<bool>                 readyXcb{false};
        std::atomic<bool>                 shuttingDown{false};
        std::atomic<wl_display*>          waylandDisplay{nullptr};
    };

    /** Stop the IPC worker before any backend registry begins destruction. */
    void stopRuntimeWorker();

    // Backend generations are process-local. Inherited generations are left
    // untouched after fork and reclaimed by the OS when the process exits.

    /**
     * @brief Returns the target-process runtime that remains resident after loading.
     * @return Shared runtime instance.
     */
    Runtime& runtime();
    /** @brief Enables the IPC runtime on first real keyboard frontend activation. */
    void ensureRuntimeInitialized();
    /** Discard inherited backend generations without touching their locks. */
    void resetX11AfterFork();
    void resetXcbAfterFork();
    void resetWaylandAfterFork();
    /** @brief Starts the IPC worker after keyboard focus or the first delivered key. */
    void noteKeyboardActivity(bool wayland);
    /** @brief Updates Xlib keyboard focus and starts lazy IPC on focus-in. */
    void noteX11Focus(bool focused);
    /** @brief Starts IPC when an Xlib window selects keyboard events, before focus-in. */
    void noteX11KeyboardAvailable(bool available = true);
    /** @brief Updates Wayland keyboard focus. */
    void noteWaylandFocus(bool focused);
    /** @brief Starts IPC when a Wayland keyboard listener is registered, before focus-in. */
    void            noteWaylandKeyboardAvailable(bool available);
    void            noteXcbFocus(bool focused);
    void            noteXcbKeyboardAvailable(bool available);
    void            markXlibOwnedXcbConnection(void* connection);
    void            markXcbKeyboardSubscribed(void* connection);
    void            forgetXlibXcbConnection(void* connection);
    void            setXcbEventQueueOwner(void* connection, bool xcbOwns, uint32_t root = 0);
    bool            xcbOwnsXlibConnection(void* connection);
    void            wakeX11();
    void            wakeXcb();
    void            loadXcb();
    FocusCandidates focusedX11Candidates();
    FocusCandidates focusedXcbCandidates();
    FocusCandidates focusedWaylandCandidates();
    /**
     * @brief Takes the next pending keyboard request.
     * @return Pending request or an empty pointer.
     */
    std::shared_ptr<Task> takeTask(HookBackend backend, const void* connection, uintptr_t focused);
    /** @brief Cancels queued requests for a frontend connection after focus changes. */
    void cancelPendingTasks(HookBackend backend, const void* connection);
    /**
     * @brief Marks a task completed while the runtime mutex is held.
     * @param task Request to complete.
     * @param status Completion status, zero on success.
     */
    void finishLocked(const std::shared_ptr<Task>& task, int status);
    /**
     * @brief Wakes the target application event dispatcher.
     */
    void wakeApplication();
    /**
     * @brief Schedules a Wayland callback to wake the event dispatcher.
     */
    void wakeWayland();
    /** @brief Exposes deferred Wayland wake flushes to the IPC worker poll loop. */
    int waylandWakeFlushFd();
    /** @brief Retries a sync callback when its Wayland socket becomes writable. */
    void retryWaylandWakeFlush();
    /**
     * @brief Resolves original Xlib symbols from an existing X11 library.
     */
    void loadX11();
    /**
     * @brief Resolves original Wayland client symbols.
     */
    void loadWayland();

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_TARGET_H_
