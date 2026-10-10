/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-uinput-device.h"

#include <unistd.h>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>

UinputDevice::~UinputDevice() {
    reset();
}

UinputDevice::UinputDevice(UinputDevice&& other) noexcept : uinput_(other.uinput_) {
    other.uinput_ = nullptr;
}

UinputDevice& UinputDevice::operator=(UinputDevice&& other) noexcept {
    if (this != &other) {
        reset();
        uinput_       = other.uinput_;
        other.uinput_ = nullptr;
    }
    return *this;
}

void UinputDevice::reset() {
    if (uinput_ != nullptr) {
        libevdev_uinput_destroy(uinput_);
        uinput_ = nullptr;
    }
}

bool UinputDevice::initialize() {
    struct libevdev* dev = libevdev_new();
    if (dev == nullptr)
        return false;
    libevdev_set_name(dev, "Lotus-Uinput-Server");
    libevdev_enable_event_code(dev, EV_KEY, KEY_BACKSPACE, nullptr);
    libevdev_enable_event_code(dev, EV_KEY, KEY_LEFT, nullptr);
    libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTSHIFT, nullptr);
    libevdev_enable_event_code(dev, EV_KEY, KEY_DELETE, nullptr);

    struct libevdev_uinput* uinput = nullptr;
    const int               rc     = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput);
    libevdev_free(dev);
    if (rc != 0)
        return false;
    uinput_ = uinput;
    // Give the compositor a beat to open the new node before the first tap,
    // same settle as the old raw-ioctl device (connects are rare; keys only
    // flow after the EIS handshake completes anyway).
    sleep(1);
    return true;
}

void UinputDevice::send_key(uint16_t code, int value) {
    if (uinput_ == nullptr)
        return;
    libevdev_uinput_write_event(uinput_, EV_KEY, code, value);
    libevdev_uinput_write_event(uinput_, EV_SYN, SYN_REPORT, 0);
}

int UinputDevice::get_fd() const {
    return uinput_ != nullptr ? libevdev_uinput_get_fd(uinput_) : -1;
}
