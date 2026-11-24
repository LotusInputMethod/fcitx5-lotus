// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

DFlickable {
    id: root

    default property alias content: mainCol.data
    property alias spacing: mainCol.spacing
    property real maxContentWidth: 700
    property real horizontalMargin: Style.spacingXL
    property real contentTopMargin: Style.spacingL
    property real bottomClearance: 76

    anchors.fill: parent
    contentWidth: width
    contentHeight: mainCol.implicitHeight + contentTopMargin + bottomClearance
    clip: true

    ColumnLayout {
        id: mainCol
        x: Math.max(root.horizontalMargin, Math.round((root.width - width) / 2))
        y: root.contentTopMargin
        width: Math.min(Math.max(0, root.width - root.horizontalMargin * 2), root.maxContentWidth)
        spacing: Style.spacingL
    }
}
