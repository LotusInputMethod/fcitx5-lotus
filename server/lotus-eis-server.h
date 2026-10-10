/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-eis-server.h
 * @brief EIS keyboard server: one seat, key events dumb-emitted to uinput.
 *
 * Clients arrive pre-authenticated (the kb socket only hands off EIS fds
 * over SCM_RIGHTS). This class owns the whole libeis lifecycle: seat/device
 * management plus one uinput node per bound keyboard. dispatch() fans out
 * to one small onXxx() method per event type — no pacing here, every key is
 * emitted immediately.
 */

#ifndef _LOTUS_EIS_SERVER_H_
#define _LOTUS_EIS_SERVER_H_

#include <vector>

struct eis;
struct eis_client;
struct eis_device;
struct eis_event;
struct eis_seat;

class UinputDevice;

class EisKeyboardServer {
  public:
    EisKeyboardServer()  = default;
    ~EisKeyboardServer();

    EisKeyboardServer(const EisKeyboardServer&)            = delete;
    EisKeyboardServer& operator=(const EisKeyboardServer&) = delete;
    EisKeyboardServer(EisKeyboardServer&&)                 = delete;
    EisKeyboardServer& operator=(EisKeyboardServer&&)      = delete;

    /**
     * @brief Creates the EIS context on the fd backend.
     * @return True on success.
     */
    bool initialize();

    /**
     * @brief Fd to poll for EIS events (stable for the context lifetime).
     */
    int eventFd() const;

    /**
     * @brief Creates the per-client EIS fd for main() to pass over the
     * authed socket. Returns the fd, or -1 on failure.
     */
    int addClient();

    /**
     * @brief Drains all pending libeis events. Call on POLLIN.
     */
    void dispatch();

  private:
    struct BoundKeyboard {
        struct eis_client* client = nullptr;
        struct eis_device* device = nullptr;
        UinputDevice*      uinput = nullptr;
    };

    void onEvent(struct eis_event* event);
    void onClientConnect(struct eis_client* client);
    void onClientDisconnect(struct eis_client* client);
    void onSeatBind(struct eis_event* event);
    void onDeviceClosed(struct eis_device* device);
    void onKeyboardKey(struct eis_event* event);
    void destroyKeyboard(BoundKeyboard& bound);

    struct eis* eis_ = nullptr;
    // Seats/devices carry a dangling owner ref, released on disconnect,
    // device close, or destruction (see destroyKeyboard + sweeps).
    std::vector<struct eis_seat*> seats_;
    std::vector<BoundKeyboard>    keyboards_;
};

#endif // _LOTUS_EIS_SERVER_H_
