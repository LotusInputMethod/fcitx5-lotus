// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import "../DCommon/Widgets" as DW

DW.DDialog {
    id: root

    embedded: false
    opened: false
    z: 1000
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    function open() {
        opened = true
        forceActiveFocus()
    }

    function close() {
        opened = false
    }

    Component.onCompleted: {
        if (Overlay.overlay)
            parent = Overlay.overlay
    }

    onRejected: close()
}
