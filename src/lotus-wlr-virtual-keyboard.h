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

#include "common-fd-guard.h"
#include "lotus-delayed-task-runner.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct wl_display;
struct wl_keyboard;
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
     * @brief Releases a wl_keyboard proxy.
     */
    struct WlKeyboardDeleter {
        /**
         * @brief Destroys the keyboard proxy.
         * @param keyboard Keyboard to release, may be null.
         */
        void operator()(wl_keyboard* keyboard) const noexcept;
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
    using WlKeyboardPtr                = std::unique_ptr<wl_keyboard, WlKeyboardDeleter>;                                    ///< Owning keymap observer
    using ZwpVirtualKeyboardManagerPtr = std::unique_ptr<zwp_virtual_keyboard_manager_v1, ZwpVirtualKeyboardManagerDeleter>; ///< Owning manager
    using ZwpVirtualKeyboardPtr        = std::unique_ptr<zwp_virtual_keyboard_v1, ZwpVirtualKeyboardDeleter>;                ///< Owning virtual keyboard

    /**
     * @brief Wayland zwp_virtual_keyboard_v1 helper.
     *
     * Sends BackSpace, Shift+Left and Unicode on one virtual-keyboard channel so the compositor preserves order.
     * Shift is set with zwp_virtual_keyboard_v1_modifiers, not a real Shift key. The modifier state is mirrored from
     * the physical keyboard so a held Shift keeps later forwarded keys uppercase and a release clears it again.
     *
     * Movements that only need BackSpace/Left/Delete run on the compositor's own keymap. A custom XKB keymap is used
     * only while Unicode keys are being injected, then the compositor keymap is restored. A different keymap can be
     * pushed to the focused client, recreating its xkb state and silently dropping held modifiers (Shift, NumLock).
     *
     * A whole burst is queued in one batch (load keymap, inject keys, restore keymap) and drained with a single
     * roundtrip. Keymap bytes live in reusable memfds; the compositor keymap is only written once and an unload
     * just re-sends its fd.
     *
     * available() only looks for the manager global; the virtual keyboard device itself is created on the first
     * emit, so simply probing the compositor has no visible side effect.
     */
    class LOTUS_EXPORT LotusWlrVirtualKeyboard {
      public:
        static LotusWlrVirtualKeyboard& instance();

        bool                            emit(int backspaces, std::string_view utf8, bool trackEcho = true, bool anchorDot = false);
        [[nodiscard]] bool              injecting() const {
            return injecting_.load(std::memory_order_acquire);
        }
        bool consumeEcho(uint32_t code, uint32_t time);

        /**
         * @brief Keeps the virtual keyboard's Shift in sync with the physical Shift key.
         *
         * Applies the state immediately and re-applies it after each keymap upload,
         * which resets the compositor's modifiers. Without the sync, a held Shift is
         * lost on forwarded keys, and a released Shift stays stuck and upcases later keys.
         *
         * @param down True while the user holds Shift.
         */
        void setUserShift(bool down);

        /**
         * @brief Keeps the virtual keyboard's Shift and locked CapsLock in sync with the user's state.
         * @param shiftDown True while the user holds Shift.
         * @param capsLockOn True while CapsLock is locked on.
         */
        void setUserModifiers(bool shiftDown, bool capsLockOn);
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
        bool                                      uploadKeymapBytes(std::string_view keymap);
        bool                                      restoreCompositorKeymap();
        static bool                               writeKeymapToFd(int fd, std::string_view keymap);
        int                                       keymapFdForUpload();
        void                                      resetKeymapFds();
        void                                      watchCompositorKeymap();
        void                                      storeCompositorKeymap(uint32_t format, int fd, uint32_t size);
        static void                               compositorKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size);
        void                                      tap(uint32_t evdevKey, bool track);
        void                                      syncModifiers(bool flush = true);
        bool                                      emitImpl(int count, std::string_view utf8, bool trackEcho, bool anchorDot);
        void                                      disconnect();

        WlDisplayPtr                              display_;          ///< Display opened on the first probe
        WlRegistryPtr                             registry_;         ///< Registry of that display
        WlSeatPtr                                 seat_;             ///< Seat the virtual keyboard is bound to
        WlKeyboardPtr                             keymapObserver_;   ///< wl_keyboard used only to read the seat keymap
        ZwpVirtualKeyboardManagerPtr              manager_;          ///< Manager global of the compositor
        ZwpVirtualKeyboardPtr                     keyboard_;         ///< Virtual keyboard device, created on the first emit
        std::string                               compositorKeymap_; ///< Exact XKB_V1 keymap the compositor uses for the seat
        std::vector<FdGuard>                      keymapFds_;        ///< Reusable memfds, one per in-flight custom upload
        size_t                                    keymapFdSlot_ = 0;
        FdGuard                                   compositorKeymapFd_;             ///< Memfd holding the cached compositor keymap
        bool                                      compositorKeymapCached_ = false; ///< Whole compositorKeymap_ already written to the fd
        uint32_t                                  time_                   = 1;
        std::atomic<bool>                         injecting_              = false;
        bool                                      probed_                 = false;
        bool                                      keymapSet_              = false;
        bool                                      keymapIsCompositor_     = false;
        bool                                      supported_              = false;
        std::atomic<bool>                         userShift_              = false;
        std::atomic<bool>                         userCapsLock_           = false;
        std::deque<std::pair<uint32_t, uint32_t>> echo_;
        std::mutex                                deviceMutex_; ///< Serialises the Wayland device: all device, keymap and fd state below requires it

        LotusDelayedTaskRunner                    runner_; ///< Delivers the replacement pre/post delay off the main thread
    };

} // namespace fcitx

#endif
