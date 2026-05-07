/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _FCITX5_LOTUS_H_
#define _FCITX5_LOTUS_H_

#include <cstdint>
#include <utility>

namespace fcitx {

class LotusEngine;
class LotusState;

class Object {
public:
    Object() noexcept = default;

    explicit Object(uintptr_t value) noexcept
        : value_(value) {}

    ~Object() = default;

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    Object(Object&& other) noexcept
        : value_(std::exchange(other.value_, 0)) {}

    Object& operator=(Object&& other) noexcept {
        if (this != &other) {
            value_ = std::exchange(other.value_, 0);
        }
        return *this;
    }

    void reset(uintptr_t value = 0) noexcept {
        value_ = value;
    }

    [[nodiscard]] uintptr_t handle() const noexcept {
        return value_;
    }

    [[nodiscard]] uintptr_t release() noexcept {
        return std::exchange(value_, 0);
    }

    explicit operator bool() const noexcept {
        return value_ != 0;
    }

private:
    uintptr_t value_ = 0;
};

} // namespace fcitx

#endif // _FCITX5_LOTUS_H_
