// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

Item {
    id: root

    property bool offline: typeof Backend !== "undefined" ? Backend.fcitxOffline : false

    readonly property real targetHeight: offline ? Math.max(48, bannerRow.implicitHeight + Style.spacingS * 2) : 0

    implicitHeight: targetHeight
    Layout.preferredHeight: targetHeight
    clip: true
    visible: targetHeight > 0

    Behavior on implicitHeight {
        enabled: !Style.reduceMotion
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Style.withAlpha(Style.warning, 0.14)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Style.withAlpha(Style.warning, 0.35)
        }

        RowLayout {
            id: bannerRow
            anchors.fill: parent
            anchors.leftMargin: Style.spacingL
            anchors.rightMargin: Style.spacingL
            spacing: Style.spacingM

            DIcon {
                name: "warning"
                size: Style.iconSizeMedium
                color: Style.warning
                Layout.alignment: Qt.AlignVCenter
            }

            StyledText {
                Layout.fillWidth: true
                text: I18n.tr("Fcitx5 is not running. Settings cannot be loaded or saved.")
                font.pixelSize: Style.fontSizeMedium
                font.weight: Style.fontWeightMedium
                color: Style.surfaceText
                wrapMode: Text.WordWrap
            }

            DButton {
                text: I18n.tr("Retry")
                iconName: "refresh"
                iconSize: Style.iconSizeSmall
                buttonHeight: Style.buttonHeightXS
                variant: "tonal"
                Layout.alignment: Qt.AlignVCenter
                onClicked: Backend.retryConnection()
            }
        }
    }
}
