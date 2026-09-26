/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus.h
 * @brief Main header file for fcitx5-lotus Vietnamese input method.
 */

#ifndef _FCITX5_LOTUS_H_
#define _FCITX5_LOTUS_H_

#include <cstdint>
#include <optional>

// Declaration only — including the generated bamboo-core.h here would put the
// whole C++ build behind the Go c-archive step (see bamboo/CMakeLists.txt).
extern "C" void DeleteObject(std::uintptr_t handle);

namespace fcitx {

    class LotusEngine;
    class LotusState;

    /**
     * @brief RAII wrapper for CGo handles.
     *
     * Manages lifecycle of Go objects accessed from C++ through uintptr_t handles.
     * Automatically releases handles on destruction.
     */
    class CGoObject {
      public:
        /**
         * @brief Constructs with optional handle.
         * @param handle Optional CGo handle.
         */
        CGoObject(std::optional<uintptr_t> handle = std::nullopt) : handle_(handle) {}

        /**
         * @brief Releases the handle on destruction.
         */
        ~CGoObject() {
            if (handle_) {
                DeleteObject(*handle_);
            }
        }

        CGoObject(const CGoObject&)            = delete;
        CGoObject& operator=(const CGoObject&) = delete;

        CGoObject(CGoObject&& other) noexcept : handle_(other.handle_) {
            other.handle_ = std::nullopt;
        }

        CGoObject& operator=(CGoObject&& other) noexcept {
            if (this != &other) {
                clear();
                handle_       = other.handle_;
                other.handle_ = std::nullopt;
            }
            return *this;
        }

        /**
         * @brief Resets with a new handle, releasing the old one.
         * @param handle New handle to store.
         */
        void reset(std::optional<uintptr_t> handle = std::nullopt) {
            clear();
            handle_ = handle;
        }

        /**
         * @brief Gets the stored handle.
         * @return The handle value or 0 if empty.
         */
        uintptr_t handle() const {
            return handle_.value_or(0);
        }

        /**
         * @brief Releases ownership of the handle.
         * @return The handle value or 0 if empty.
         */
        uintptr_t release() {
            if (handle_) {
                uintptr_t v = *handle_;
                handle_     = std::nullopt;
                return v;
            }
            return 0;
        }

        /**
         * @brief Checks if a valid handle is stored.
         * @return True if handle exists and is non-zero.
         */
        explicit operator bool() const {
            return handle_.has_value() && *handle_ != 0;
        }

      private:
        /**
         * @brief Releases the current handle.
         */
        void clear() {
            if (handle_) {
                DeleteObject(*handle_);
                handle_ = std::nullopt;
            }
        }

        std::optional<uintptr_t> handle_;
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_H_
