/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-virtual-keyboard.h
 * @brief libei sender for emulated keys (addon -> EIS server).
 *
 * Single responsibility: own the libei client lifecycle and send one key
 * event at a time. No sleep, no thread, no queue — pacing lives in KeyPacer.
 *
 * The EIS fd arrives over the authenticated kb socket (SCM_RIGHTS); every
 * key then travels as an EIS keyboard event (key + frame per call).
 *
 * Socket selection: a path starting with '/' is a filesystem EI socket and
 * uses the direct protocol (connect + EI handshake, no auth gate); anything
 * else is an abstract-namespace auth socket with the SCM_RIGHTS handoff.
 */

#ifndef _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_
#define _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct ei;
struct ei_device;
struct ei_event;
struct ei_seat;

namespace fcitx {

    class EventLoop;
    class EventSourceIO;

    class VirtualKeyboard {
      public:
        VirtualKeyboard() = default;
        ~VirtualKeyboard();

        VirtualKeyboard(const VirtualKeyboard&)            = delete;
        VirtualKeyboard& operator=(const VirtualKeyboard&) = delete;
        VirtualKeyboard(VirtualKeyboard&&)                 = delete;
        VirtualKeyboard& operator=(VirtualKeyboard&&)      = delete;

        /**
     * @brief Binds the fcitx event loop for EIS dispatch. Called once from
     * KeyPacer::init (the loop outlives this singleton).
     */
        void init(EventLoop* loop);

        /**
     * @brief Sets the callback fired once when the keyboard device becomes
     * emulatable (first RESUMED, or re-RESUMED after a pause). KeyPacer uses
     * it to re-pump a queue held while not ready.
     */
        void setResumeCallback(std::function<void()> callback);

        /**
     * @brief Overrides the socket path (tests only; default is kb_socket).
     * A leading '/' selects the direct EI filesystem-socket mode.
     */
        void setSocketPath(std::string path);

        /**
     * @brief Runs the EIS handshake if needed: connects the auth socket,
     * receives the EIS fd (SCM_RIGHTS) and sets up the libei sender.
     * @return True if emulatable now or the handshake is in flight.
     */
        bool ensureConnected();

        /**
     * @brief Sends one key event (key + frame). No reconnect, no sleep.
     * @return True if the event was handed to libei (device emulatable).
     */
        bool send(uint16_t code, uint8_t pressed);

        /**
     * @brief Whether the keyboard device is currently emulatable.
     */
        bool isReady() const;

        /**
     * @brief Tears down the libei context. Idempotent.
     */
        void close();

      private:
        void onEiReadable();
        // Handles one libei event. Returns true on a !ready -> ready transition.
        // Requires m_ held.
        bool handleEiEventLocked(struct ei_event* event);
        // Drops the libei context and device refs. Requires m_ held.
        void                           teardownEiLocked();

        std::string                    path_; ///< override for tests; empty = $LOTUS_EIS_SOCKET or kb_socket default; leading '/' = direct EI socket
        mutable std::mutex             m_;
        EventLoop*                     loop_{nullptr};
        std::function<void()>          resumeCallback_;
        std::unique_ptr<EventSourceIO> io_;
        struct ei*                     ei_{nullptr};
        struct ei_seat*                seat_{nullptr};
        struct ei_device*              kbd_{nullptr};
        bool                           ready_{false};
        uint32_t                       seq_{0};
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_VIRTUAL_KEYBOARD_H_
