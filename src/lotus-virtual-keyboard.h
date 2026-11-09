/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_
#define _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

namespace fcitx {

    /**
     * Wayland zwp_virtual_keyboard_v1 helper.
     *
     * Sends BackSpace, Shift+Left and Unicode on one virtual-keyboard channel
     * so the compositor preserves order. Shift is set with
     * zwp_virtual_keyboard_v1_modifiers, not a real Shift key, so it does not
     * leak into the seat and upcase later characters. A custom XKB keymap maps
     * spare keycodes to the needed characters.
     */
    class LotusVirtualKeyboard {
      public:
        static LotusVirtualKeyboard& instance();

        bool                         emit(int backspaces, std::string_view utf8, bool trackEcho = true);
        // Shift+Left via zwp_virtual_keyboard_v1_modifiers, not a real Shift key.
        bool               emitSelect(int count, std::string_view utf8, bool trackEcho = true);
        [[nodiscard]] bool injecting() const {
            return injecting_;
        }
        bool consumeEcho(uint32_t code, uint32_t time);

        /**
         * @brief Waits @p delayMs on the virtual keyboard worker thread, then
         * runs @p task there.
         *
         * Used to keep the replacement post-delay off the fcitx main thread.
         * The task must hand any fcitx state work back to the event loop.
         */
        void runAfter(int delayMs, std::function<void()> task);
        /**
         * @brief Drops queued delayed tasks and joins the worker.
         *
         * Called before the owning engine goes away so a pending task can no
         * longer reach the event loop or the engine.
         */
        void cancelPendingTasks();

        // Probes the current compositor once for zwp_virtual_keyboard_manager_v1.
        bool available();

        LotusVirtualKeyboard(const LotusVirtualKeyboard&)            = delete;
        LotusVirtualKeyboard& operator=(const LotusVirtualKeyboard&) = delete;
        LotusVirtualKeyboard(LotusVirtualKeyboard&&)                 = delete;
        LotusVirtualKeyboard& operator=(LotusVirtualKeyboard&&)      = delete;

      private:
        LotusVirtualKeyboard() = default;
        ~LotusVirtualKeyboard();

        bool ensureConnected();
        bool uploadKeymap(const uint32_t* cps, size_t n);
        void tap(uint32_t evdevKey, bool track);
        void setShift(bool down);
        bool emitImpl(int count, std::string_view utf8, bool trackEcho, bool shiftLeft);
        void disconnect();
        void workerLoop();

        struct DelayedTask {
            std::chrono::steady_clock::time_point due;
            uint64_t                              generation;
            std::function<void()>                 task;
        };

        ::wl_display*                             display_   = nullptr;
        ::wl_registry*                            registry_  = nullptr;
        ::wl_seat*                                seat_      = nullptr;
        ::zwp_virtual_keyboard_manager_v1*        manager_   = nullptr;
        ::zwp_virtual_keyboard_v1*                keyboard_  = nullptr;
        uint32_t                                  time_      = 1;
        bool                                      injecting_ = false;
        bool                                      probed_    = false;
        bool                                      supported_ = false;
        std::deque<std::pair<uint32_t, uint32_t>> echo_;

        std::thread                               worker_;
        std::mutex                                workerMutex_;
        std::condition_variable                   workerCv_;
        std::vector<DelayedTask>                  delayedTasks_;
        uint64_t                                  taskGeneration_ = 0;
        bool                                      workerStop_     = false;
    };

} // namespace fcitx

#endif
