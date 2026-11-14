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

#include "lotus-apphook-elfhook.h"
#include "lotus-apphook-protocol.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

struct wl_display;

namespace lotus::apphook {

    /**
     * @brief An in-flight keyboard request and its completion state.
     */
    struct Task {
        uint64_t                  requestId = 0;
        std::vector<KeyOperation> ops;
        size_t                    x11Remaining = 0;
        int                       status       = 0;
        bool                      done         = false;
    };

    /**
     * @brief Shared request queue and input backend state in the target process.
     */
    struct Runtime {
        std::mutex                        mutex;
        std::atomic<bool>                 workersStarted{false};
        std::atomic<bool>                 initialized{false};
        std::atomic<bool>                 lateAttached{false};
        std::atomic<bool>                 focusedX11{false};
        std::atomic<bool>                 focusedWayland{false};
        std::deque<std::shared_ptr<Task>> pending;
        std::shared_ptr<Task>             activeX11;
        size_t                            inFlight = 0;
        std::atomic<int>                  wakeFd{-1};
        std::atomic<int>                  completionFd{-1};
        std::atomic<bool>                 readyX11{false};
        std::atomic<bool>                 readyWayland{false};
        std::atomic<bool>                 shuttingDown{false};
        std::atomic<wl_display*>          waylandDisplay{nullptr};
    };

    /**
     * @brief Returns the target-process runtime that remains resident after loading.
     * @return Shared runtime instance.
     */
    Runtime& runtime();
    /** @brief Starts the IPC worker after keyboard focus or the first delivered key. */
    void noteKeyboardActivity(bool wayland);
    /** @brief Updates Xlib keyboard focus and starts lazy IPC on focus-in. */
    void noteX11Focus(bool focused);
    /** @brief Updates Wayland keyboard focus and starts lazy IPC on focus-in. */
    void noteWaylandFocus(bool focused);
    /**
     * @brief Takes the next pending keyboard request.
     * @return Pending request or an empty pointer.
     */
    std::shared_ptr<Task> takeTask();
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
    /**
     * @brief Resolves original Xlib symbols from an existing X11 library.
     */
    void loadX11();
    /**
     * @brief Resolves original Wayland client symbols.
     */
    void loadWayland();
    /**
     * @brief Patches Xlib keyboard event entry points.
     * @return Number of newly patched relocation slots.
     */
    int installX11Hooks();
    /**
     * @brief Patches Wayland client keyboard event entry points.
     * @return Number of newly patched relocation slots.
     */
    int installWaylandHooks();

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_TARGET_H_
