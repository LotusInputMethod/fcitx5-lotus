// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

Item {
    id: root

    property bool active: false
    property string message: ""

    function show(text) {
        if (!text)
            return;
        message = text;
        active = true;
        dismissTimer.restart();
    }

    function hide() {
        dismissTimer.stop();
        active = false;
    }

    Connections {
        target: typeof Backend !== "undefined" ? Backend : null
        ignoreUnknownSignals: true
        function onToastRequested(msg) {
            root.show(msg);
        }
    }

    Timer {
        id: dismissTimer
        interval: 3200
        onTriggered: root.active = false
    }

    readonly property real maxBannerWidth: Math.min(480, parent ? parent.width - Style.spacingXL * 2 : 420)

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight
    width: implicitWidth
    height: implicitHeight
    visible: opacity > 0.01
    opacity: active ? 1 : 0

    transform: Translate {
        y: root.active ? 0 : 12
        Behavior on y {
            enabled: !Style.reduceMotion
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }
    }

    Behavior on opacity {
        enabled: !Style.reduceMotion
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    ElevationShadow {
        anchors.fill: pill
        level: Style.elevationLevel3
        targetRadius: pill.radius
        targetColor: Style.tooltipSurface
        shadowEnabled: Style.elevationEnabled
    }

    Rectangle {
        id: pill
        anchors.centerIn: parent
        implicitWidth: Math.min(root.maxBannerWidth, row.implicitWidth + Style.spacingL * 2)
        implicitHeight: Math.max(40, row.implicitHeight + Style.spacingS * 2)
        width: implicitWidth
        height: implicitHeight
        radius: height / 2
        color: Style.tooltipSurface

        RowLayout {
            id: row
            anchors.centerIn: parent
            width: Math.min(root.maxBannerWidth - Style.spacingL * 2, implicitWidth)
            spacing: Style.spacingS

            StyledText {
                Layout.fillWidth: true
                text: root.message
                color: Style.tooltipText
                font.pixelSize: Style.fontSizeMedium
                font.weight: Style.fontWeightMedium
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                horizontalAlignment: Text.AlignHCenter
            }

            DActionButton {
                iconName: "close"
                iconSize: Style.iconSizeSmall
                buttonSize: Style.buttonHeightXXS
                iconColor: Style.tooltipText
                tooltipText: I18n.tr("Dismiss")
                onClicked: root.hide()
            }
        }
    }
}
