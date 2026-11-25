// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// Qt 6.2 compatibility: Item.focusPolicy (ClickFocus/StrongFocus) only exists since Qt 6.7.
// This handler reproduces click-to-focus on older Qt; tab focus is handled with activeFocusOnTab.
import QtQuick

TapHandler {
    id: root

    property Item target: null

    onTapped: {
        if (root.enabled && root.target && root.target.enabled && root.target.visible) {
            root.target.forceActiveFocus(Qt.MouseFocusReason);
        }
    }
}
