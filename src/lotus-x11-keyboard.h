/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file lotus-x11-keyboard.h
 * @brief X11 keyboard backend built on the XTEST extension.
 */

#ifndef _LOTUS_X11_KEYBOARD_H_
#define _LOTUS_X11_KEYBOARD_H_

#include "lotus-delayed-task-runner.h"
#include "lotus-x11.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace fcitx {

    /**
     * @brief Injects keys into the X server through XTEST.
     *
     * Behaves like the wlroots virtual keyboard: BackSpace, Shift+Left, Delete and text are sent as real key events,
     * so the focused client and the input method both see them in order. Text uses the keysyms of spare keycodes,
     * which are remapped for the burst and always restored afterwards. All X calls run on the shared runner thread,
     * where the mandatory per-key interval is spent, so the fcitx main thread never sleeps.
     */
    class LOTUS_EXPORT LotusX11Keyboard {
      public:
        static LotusX11Keyboard& instance();

        /**
         * @brief Probes the X server once for the XTEST extension.
         * @return True when keys can be injected.
         */
        bool available();

        /**
         * @brief Reports whether either Shift key is currently held on the X keyboard.
         * @return True when Shift is down, or when the state cannot be read (fail safe).
         */
        [[nodiscard]] static bool shiftHeld();

        /**
         * @brief Queues a replacement burst.
         * @param backspaces Number of BackSpace (or Shift+Left when @p select) events.
         * @param utf8 Text to type after the deletions; may be empty.
         * @param intervalMs Delay between two injected keys, at least 1 ms.
         * @param select True to select with Shift+Left and Delete instead of backspacing.
         * @return True when the burst was queued.
         */
        bool emit(int backspaces, std::string_view utf8, int intervalMs, bool select);

        /**
         * @brief Hands a delayed task to the shared runner.
         * @param delayMs Delay in milliseconds.
         * @param task Task to run on the worker thread.
         */
        void runAfter(int delayMs, std::function<void()> task) {
            runner_.runAfter(delayMs, std::move(task));
        }

        /**
         * @brief Drops queued bursts and joins the worker.
         */
        void cancelPendingTasks();

        LotusX11Keyboard(const LotusX11Keyboard&)            = delete;
        LotusX11Keyboard& operator=(const LotusX11Keyboard&) = delete;
        LotusX11Keyboard(LotusX11Keyboard&&)                 = delete;
        LotusX11Keyboard& operator=(LotusX11Keyboard&&)      = delete;

      private:
        LotusX11Keyboard() = default;
        ~LotusX11Keyboard();

        xcb_connection_t*      ensureConnection();
        void                   runBurst(int backspaces, const std::vector<uint32_t>& cps, int intervalMs, bool select);

        std::mutex             connectionMutex_;   ///< Guards the worker connection
        XcbConnectionPtr       connection_;        ///< X server connection owned by the runner thread
        std::atomic<bool>      cancelled_{false};  ///< Set by cancelPendingTasks() to cut a burst short
        bool                   probed_    = false; ///< The probe already ran
        bool                   supported_ = false; ///< The probe found XTEST
        LotusDelayedTaskRunner runner_;            ///< Paces the injected keys off the main thread
    };

} // namespace fcitx

#endif
