/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-eis-server.h"
#include "lotus-logger.h"
#include "lotus-uinput-device.h"

#include <string>

#include <libeis.h>
#include <linux/input-event-codes.h>

EisKeyboardServer::~EisKeyboardServer() {
    for (auto& bound : keyboards_)
        destroyKeyboard(bound);
    keyboards_.clear();
    for (auto* seat : seats_)
        eis_seat_unref(seat);
    seats_.clear();
    if (eis_ != nullptr) {
        eis_unref(eis_);
        eis_ = nullptr;
    }
}

bool EisKeyboardServer::initialize() {
    eis_ = eis_new(nullptr);
    return eis_ != nullptr && eis_setup_backend_fd(eis_) == 0;
}

int EisKeyboardServer::eventFd() const {
    return eis_ != nullptr ? eis_get_fd(eis_) : -1;
}

int EisKeyboardServer::addClient() {
    if (eis_ == nullptr)
        return -1;
    return eis_backend_fd_add_client(eis_);
}

void EisKeyboardServer::dispatch() {
    if (eis_ == nullptr)
        return;
    eis_dispatch(eis_);
    struct eis_event* event = nullptr;
    while ((event = eis_get_event(eis_)) != nullptr) {
        onEvent(event);
        eis_event_unref(event);
    }
}

void EisKeyboardServer::onEvent(struct eis_event* event) {
    switch (eis_event_get_type(event)) {
        case EIS_EVENT_CLIENT_CONNECT:
            onClientConnect(eis_event_get_client(event));
            break;
        case EIS_EVENT_CLIENT_DISCONNECT:
            onClientDisconnect(eis_event_get_client(event));
            break;
        case EIS_EVENT_SEAT_BIND:
            onSeatBind(event);
            break;
        case EIS_EVENT_DEVICE_CLOSED:
            onDeviceClosed(eis_event_get_device(event));
            break;
        case EIS_EVENT_KEYBOARD_KEY:
            onKeyboardKey(event);
            break;
        default:
            break;
    }
}

void EisKeyboardServer::onClientConnect(struct eis_client* client) {
    // Pre-authenticated at the SCM_RIGHTS gate; connect at once.
    eis_client_connect(client);
    LotusLogger::instance().info("EIS client connected, creating seat 'default'");
    struct eis_seat* seat = eis_client_new_seat(client, "default");
    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
    eis_seat_add(seat);
    seats_.push_back(seat);
}

void EisKeyboardServer::onClientDisconnect(struct eis_client* client) {
    LotusLogger::instance().info("EIS client disconnected, releasing its devices");
    for (auto it = keyboards_.begin(); it != keyboards_.end();) {
        if (it->client == client) {
            destroyKeyboard(*it);
            it = keyboards_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = seats_.begin(); it != seats_.end();) {
        if (eis_seat_get_client(*it) == client) {
            eis_seat_unref(*it);
            it = seats_.erase(it);
        } else {
            ++it;
        }
    }
}

void EisKeyboardServer::onSeatBind(struct eis_event* event) {
    if (!eis_event_seat_has_capability(event, EIS_DEVICE_CAP_KEYBOARD))
        return;
    struct eis_seat* seat   = eis_event_get_seat(event);
    auto*            uinput = new UinputDevice();
    if (!uinput->initialize()) {
        LotusLogger::instance().error("Failed to initialize uinput device for EIS client");
        delete uinput;
        return;
    }
    struct eis_device* device = eis_seat_new_device(seat);
    eis_device_configure_name(device, "Lotus-Uinput-Server");
    eis_device_configure_capability(device, EIS_DEVICE_CAP_KEYBOARD);
    eis_device_set_user_data(device, uinput);
    eis_device_add(device);
    eis_device_resume(device);
    keyboards_.push_back({eis_seat_get_client(seat), device, uinput});
}

void EisKeyboardServer::onDeviceClosed(struct eis_device* device) {
    for (auto it = keyboards_.begin(); it != keyboards_.end(); ++it) {
        if (it->device == device) {
            destroyKeyboard(*it);
            keyboards_.erase(it);
            return;
        }
    }
}

void EisKeyboardServer::onKeyboardKey(struct eis_event* event) {
    struct eis_device* device = eis_event_get_device(event);
    const uint32_t key   = eis_event_keyboard_get_key(event);
    const bool     press = eis_event_keyboard_get_key_is_press(event);
    for (auto& bound : keyboards_) {
        if (bound.device == device && bound.uinput != nullptr) {
            if (key > KEY_MAX) {
                LotusLogger::instance().warn("EIS key out of range (" + std::to_string(key) + "), dropped");
            } else {
                bound.uinput->send_key(static_cast<uint16_t>(key), press ? 1 : 0);
            }
            return;
        }
    }
}

void EisKeyboardServer::destroyKeyboard(BoundKeyboard& bound) {
    delete bound.uinput;
    bound.uinput = nullptr;
    if (bound.device != nullptr) {
        eis_device_remove(bound.device);
        eis_device_unref(bound.device);
        bound.device = nullptr;
    }
}
