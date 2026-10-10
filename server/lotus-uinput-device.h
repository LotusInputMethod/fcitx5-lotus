/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Quang Minh <minhnbnt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-uinput-device.h
 * @brief RAII libevdev uinput device: emits EV_KEY events, no pacing.
 */

#ifndef _LOTUS_UINPUT_DEVICE_H_
#define _LOTUS_UINPUT_DEVICE_H_

#include <cstdint>

struct libevdev_uinput;

/**
 * @brief RAII Wrapper for a libevdev uinput device.
 * Handles create/destroy automatically. Move-only: moving transfers the
 * underlying device, the source is left empty.
 */
class UinputDevice {
  public:
    UinputDevice() = default;
    ~UinputDevice();

    // Disable copy, allow move (transfer ownership, null the source).
    UinputDevice(const UinputDevice&)            = delete;
    UinputDevice& operator=(const UinputDevice&) = delete;
    UinputDevice(UinputDevice&& other) noexcept;
    UinputDevice& operator=(UinputDevice&& other) noexcept;

    bool          initialize();
    /**
     * @brief Emits one EV_KEY event with its SYN immediately. No pacing here.
     * @param code Linux KEY_* code.
     * @param value 1 = press, 0 = release.
     */
    void              send_key(uint16_t code, int value);
    [[nodiscard]] int get_fd() const;

  private:
    void                    reset();

    struct libevdev_uinput* uinput_{nullptr};
};

#endif // _LOTUS_UINPUT_DEVICE_H_
