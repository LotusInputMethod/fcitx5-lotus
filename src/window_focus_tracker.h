/*
 * SPDX-FileCopyrightText: 2026 kimxuanhong <kimxuanhong@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FCITX5_LOTUS_WINDOW_FOCUS_TRACKER_H
#define FCITX5_LOTUS_WINDOW_FOCUS_TRACKER_H

#include <atomic>
#include <string>
#include <thread>

namespace fcitx {
    class WindowFocusTracker {
      public:
        WindowFocusTracker() = default;
        ~WindowFocusTracker();

        WindowFocusTracker(const WindowFocusTracker&)            = delete;
        WindowFocusTracker& operator=(const WindowFocusTracker&) = delete;

        bool                start();
        void                stop();

        [[nodiscard]] bool  isValid() const {
            return valid_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool isLikeAddressBarChromium() const {
            return browserUIFocused_.load(std::memory_order_relaxed);
        }

      private:
        void              threadFunc();

        std::string       focusBus_;
        std::string       focusPath_;
        std::thread       thread_;
        std::atomic<bool> valid_{false};
        std::atomic<bool> stopRequested_{false};
        std::atomic<bool> browserUIFocused_{false};
    };
}

#endif