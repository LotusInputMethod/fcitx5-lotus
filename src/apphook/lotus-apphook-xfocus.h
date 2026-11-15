/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _SRC_LOTUS_APPHOOK_XFOCUS_H_
#define _SRC_LOTUS_APPHOOK_XFOCUS_H_

namespace lotus::apphook {

    // Per-display/per-connection keyboard focus state shared by Xlib and XCB.
    template <typename Window, Window Invalid>
    struct XFocusState {
        Window target        = Invalid;
        Window focus         = Invalid;
        bool   observedFocus = false;
        bool   focused       = false;

        void   enter(Window owner) {
            target        = owner;
            focus         = owner;
            focused       = true;
            observedFocus = true;
        }

        void leave(Window owner, bool clearUnobserved = false) {
            if ((clearUnobserved && !observedFocus) || focus == owner) {
                target  = Invalid;
                focus   = Invalid;
                focused = false;
            }
            observedFocus = true;
        }

        [[nodiscard]] bool acceptsKey() const {
            return !observedFocus || focused;
        }

        void keyTarget(Window window, bool respectFocus = true) {
            if (!respectFocus || acceptsKey())
                target = window;
        }
    };

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_XFOCUS_H_
