/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _SRC_LOTUS_APPHOOK_SEQUENCE_H_
#define _SRC_LOTUS_APPHOOK_SEQUENCE_H_

#include "lotus-apphook-keys.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace lotus::apphook {

    struct KeyEdge {
        uint32_t keycode;
        uint32_t modifiers;
        bool     pressed;
    };

    // Only tracks key-edge progress. The backend owns ordering, dispatch and task lifetime.
    class KeySequence {
      public:
        KeySequence() = default;
        explicit KeySequence(std::span<const KeyOperation> operations) : operations_(operations) {}

        void reset(std::span<const KeyOperation> operations = {}) {
            operations_  = operations;
            operation_   = 0;
            tap_         = 0;
            releaseNext_ = false;
        }

        [[nodiscard]] const KeyOperation* operation() const {
            return operation_ < operations_.size() ? &operations_[operation_] : nullptr;
        }

        [[nodiscard]] std::optional<KeyEdge> current() const {
            const auto* op = operation();
            if (!op)
                return std::nullopt;
            return KeyEdge{op->keycode, op->modifiers, !releaseNext_};
        }

        [[nodiscard]] bool needsRelease() const {
            return releaseNext_;
        }
        [[nodiscard]] bool completed() const {
            return operation_ == operations_.size();
        }

        void advance() {
            const auto* op = operation();
            if (!op)
                return;
            if (!releaseNext_) {
                releaseNext_ = true;
            } else {
                releaseNext_ = false;
                if (++tap_ == op->count) {
                    tap_ = 0;
                    ++operation_;
                }
            }
        }

      private:
        std::span<const KeyOperation> operations_{};
        size_t                        operation_   = 0;
        uint32_t                      tap_         = 0;
        bool                          releaseNext_ = false;
    };

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_SEQUENCE_H_
