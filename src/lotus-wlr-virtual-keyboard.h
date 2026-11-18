/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file lotus-wlr-virtual-keyboard.h
 * @brief wlroots keyboard backend built on zwp_virtual_keyboard_v1.
 */

#ifndef _LOTUS_WLR_VIRTUAL_KEYBOARD_H_
#define _LOTUS_WLR_VIRTUAL_KEYBOARD_H_

#include "lotus-delayed-task-runner.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

namespace fcitx {

    /**
     * @brief Releases a Wayland display connection.
     */
    struct WlDisplayDeleter {
        /**
         * @brief Disconnects the display.
         * @param display Display to release, may be null.
         */
        void operator()(wl_display* display) const noexcept;
    };

    /**
     * @brief Releases a Wayland registry proxy.
     */
    struct WlRegistryDeleter {
        /**
         * @brief Destroys the registry proxy.
         * @param registry Registry to release, may be null.
         */
        void operator()(wl_registry* registry) const noexcept;
    };

    /**
     * @brief Releases a Wayland seat proxy.
     */
    struct WlSeatDeleter {
        /**
         * @brief Destroys the seat proxy.
         * @param seat Seat to release, may be null.
         */
        void operator()(wl_seat* seat) const noexcept;
    };

    /**
     * @brief Releases a zwp_virtual_keyboard_manager_v1 proxy.
     */
    struct ZwpVirtualKeyboardManagerDeleter {
        /**
         * @brief Destroys the manager proxy.
         * @param manager Manager to release, may be null.
         */
        void operator()(zwp_virtual_keyboard_manager_v1* manager) const noexcept;
    };

    /**
     * @brief Releases a zwp_virtual_keyboard_v1 proxy.
     */
    struct ZwpVirtualKeyboardDeleter {
        /**
         * @brief Destroys the virtual keyboard proxy.
         * @param keyboard Virtual keyboard to release, may be null.
         */
        void operator()(zwp_virtual_keyboard_v1* keyboard) const noexcept;
    };

    using WlDisplayPtr                 = std::unique_ptr<wl_display, WlDisplayDeleter>;                                      ///< Owning display
    using WlRegistryPtr                = std::unique_ptr<wl_registry, WlRegistryDeleter>;                                    ///< Owning registry
    using WlSeatPtr                    = std::unique_ptr<wl_seat, WlSeatDeleter>;                                            ///< Owning seat
    using ZwpVirtualKeyboardManagerPtr = std::unique_ptr<zwp_virtual_keyboard_manager_v1, ZwpVirtualKeyboardManagerDeleter>; ///< Owning manager
    using ZwpVirtualKeyboardPtr        = std::unique_ptr<zwp_virtual_keyboard_v1, ZwpVirtualKeyboardDeleter>;                ///< Owning virtual keyboard

    /**
     * @brief Wayland zwp_virtual_keyboard_v1 helper.
     *
     * Sends BackSpace, Shift+Left and Unicode on one virtual-keyboard channel so the compositor preserves order.
     * Shift is set with zwp_virtual_keyboard_v1_modifiers, not a real Shift key, so it does not leak into the seat
     * and upcase later characters. A custom XKB keymap maps spare keycodes to the needed characters.
     *
     * available() only looks for the manager global; the virtual keyboard device itself is created on the first
     * emit, so simply probing the compositor has no visible side effect.
     */
    class LOTUS_EXPORT LotusWlrVirtualKeyboard {
      public:
        static LotusWlrVirtualKeyboard& instance();

        bool                            emit(int backspaces, std::string_view utf8, bool trackEcho = true);
        // Shift+Left via zwp_virtual_keyboard_v1_modifiers, not a real Shift key.
        bool               emitSelect(int count, std::string_view utf8, bool trackEcho = true);
        [[nodiscard]] bool injecting() const {
            return injecting_;
        }
        bool consumeEcho(uint32_t code, uint32_t time);

        /**
         * @brief Hands a delayed task to the shared runner.
         * @param delayMs Delay in milliseconds.
         * @param task Task to run on the worker thread.
         */
        void runAfter(int delayMs, std::function<void()> task) {
            runner_.runAfter(delayMs, std::move(task));
        }

        /**
         * @brief Drops queued delayed tasks and joins the worker.
         */
        void cancelPendingTasks() {
            runner_.cancelPendingTasks();
        }

        /**
         * @brief Probes the compositor once for zwp_virtual_keyboard_manager_v1.
         * @return True when the compositor can take a virtual keyboard.
         */
        bool available();

        LotusWlrVirtualKeyboard(const LotusWlrVirtualKeyboard&)            = delete;
        LotusWlrVirtualKeyboard& operator=(const LotusWlrVirtualKeyboard&) = delete;
        LotusWlrVirtualKeyboard(LotusWlrVirtualKeyboard&&)                 = delete;
        LotusWlrVirtualKeyboard& operator=(LotusWlrVirtualKeyboard&&)      = delete;

      private:
        LotusWlrVirtualKeyboard() = default;
        ~LotusWlrVirtualKeyboard();

        bool                                      probe();
        bool                                      ensureDevice();
        bool                                      uploadKeymap(const uint32_t* cps, size_t n);
        void                                      tap(uint32_t evdevKey, bool track);
        void                                      setShift(bool down);
        bool                                      emitImpl(int count, std::string_view utf8, bool trackEcho, bool shiftLeft);
        void                                      disconnect();

        WlDisplayPtr                              display_;  ///< Display opened on the first probe
        WlRegistryPtr                             registry_; ///< Registry of that display
        WlSeatPtr                                 seat_;     ///< Seat the virtual keyboard is bound to
        ZwpVirtualKeyboardManagerPtr              manager_;  ///< Manager global of the compositor
        ZwpVirtualKeyboardPtr                     keyboard_; ///< Virtual keyboard device, created on the first emit
        uint32_t                                  time_      = 1;
        bool                                      injecting_ = false;
        bool                                      probed_    = false;
        bool                                      supported_ = false;
        std::deque<std::pair<uint32_t, uint32_t>> echo_;

        LotusDelayedTaskRunner                    runner_; ///< Delivers the replacement pre/post delay off the main thread
    };

} // namespace fcitx

#endif
