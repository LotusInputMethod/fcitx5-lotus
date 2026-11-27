// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import DCommon.Common
import DCommon.Widgets

StyledButton {
    id: root

    property string hotkey: ""
    property string emptyText: I18n.tr("Disabled")
    property bool conflict: false
    property bool allowSingleKey: true
    property bool singleKeyOnly: false
    property string dialogTitle: ""
    property bool autoOpenDialog: true

    signal hotkeyCaptured(string newHotkey)

    readonly property var keyParts: {
        if (!hotkey || hotkey === "" || typeof Backend === "undefined")
            return [];
        return Backend.prettyHotkeyParts(hotkey) || [];
    }

    implicitWidth: Math.max(76, contentRow.implicitWidth + Style.spacingM * 2)
    implicitHeight: Math.max(Style.buttonHeightXS + 4, contentRow.implicitHeight + Style.spacingXS * 2)
    radius: Style.cornerRadiusS
    color: {
        if (!enabled)
            return Style.onSurface_10;
        if (conflict)
            return Style.withAlpha(Style.errorContainer, 0.75);
        return hovered || visualFocus ? Style.surfaceContainerHighest : Style.surfaceContainerHigh;
    }
    border.width: conflict ? Style.outlineWidthFocused : Style.outlineWidth
    border.color: conflict ? Style.error : (visualFocus ? Style.primary : Style.outlineVariant)

    Accessible.name: dialogTitle || (keyParts.length > 0 ? keyParts.join("+") : emptyText)

    onClicked: {
        if (autoOpenDialog)
            captureDialog.openFor(dialogTitle, hotkey, allowSingleKey, singleKeyOnly);
    }

    FocusRing {
        visible: root.visualFocus
        radius: root.radius + Style.focusRingOffset
    }

    StateLayer {
        control: root
        disabled: !root.enabled
        stateColor: root.conflict ? Style.error : Style.surfaceText
        cornerRadius: root.radius
    }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: Style.spacingXS

        Repeater {
            model: root.keyParts
            DKeycap {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                text: String(modelData)
                textColor: root.conflict ? Style.error : (root.enabled ? Style.surfaceText : Style.onSurface_38)
            }
        }

        StyledText {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.keyParts.length === 0
            text: root.emptyText
            font.pixelSize: Style.fontSizeMedium
            font.weight: Style.fontWeightMedium
            color: root.conflict ? Style.error : Style.surfaceVariantText
        }
    }

    HotkeyCaptureDialog {
        id: captureDialog
        onCaptured: newKey => root.hotkeyCaptured(newKey)
    }
}
