// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

Item {
    id: root

    property bool active: typeof Backend !== "undefined" ? Backend.hasUnsavedChanges : false
    property bool canApply: typeof Backend !== "undefined" ? !Backend.hasValidationErrors : true
    property string validationMessage: typeof Backend !== "undefined" ? Backend.validationMessage : ""

    implicitWidth: capsule.implicitWidth
    implicitHeight: capsule.implicitHeight
    width: implicitWidth
    height: implicitHeight
    visible: opacity > 0.01
    enabled: active
    opacity: active ? 1 : 0

    transform: Translate {
        y: root.active ? 0 : 24
        Behavior on y {
            enabled: !Style.reduceMotion
            DAnim {
                duration: Style.expressiveDurations.expressiveFastSpatial
                easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
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
        anchors.fill: capsule
        level: Style.elevationLevel3
        targetRadius: capsule.radius
        targetColor: Style.surfaceContainerHigh
        borderColor: Style.outlineVariant
        borderWidth: Style.outlineWidth
        shadowEnabled: Style.elevationEnabled
    }

    Rectangle {
        id: capsule
        implicitWidth: barRow.implicitWidth + Style.spacingM * 2
        implicitHeight: barRow.implicitHeight + Style.spacingS * 2
        width: implicitWidth
        height: implicitHeight
        radius: height / 2
        color: Style.surfaceContainerHigh
        border.width: Style.outlineWidth
        border.color: Style.outlineVariant

        RowLayout {
            id: barRow
            anchors.centerIn: parent
            spacing: Style.spacingS

            DButton {
                text: I18n.tr("Cancel")
                variant: "text"
                buttonHeight: Style.buttonHeightS
                onClicked: Backend.cancelAll()
            }

            DButton {
                text: I18n.tr("Apply")
                variant: "filled"
                buttonHeight: Style.buttonHeightS
                enabled: root.canApply
                tooltipText: (!root.canApply && root.validationMessage !== "") ? root.validationMessage : null
                tooltipSide: "top"
                onClicked: {
                    const win = root.Window.window;
                    if (win && win.activeFocusItem)
                        win.activeFocusItem.focus = false;
                    Backend.applyAll();
                }
            }
        }
    }
}
