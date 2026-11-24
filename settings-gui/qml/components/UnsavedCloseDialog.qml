// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import DCommon.Common
import DCommon.Widgets

OverlayDialog {
    id: root

    property bool hasValidationErrors: typeof Backend !== "undefined" ? Backend.hasValidationErrors : false
    property string validationMessage: typeof Backend !== "undefined" ? Backend.validationMessage : ""

    signal discardRequested
    signal applyAndCloseRequested

    function openDialog() {
        open();
    }

    maximumWidth: 420
    title: I18n.tr("Unsaved Changes")
    supportingText: I18n.tr("You have unsaved settings. Save before closing?")
    acceptEnabled: !hasValidationErrors

    onAccepted: {
        close();
        applyAndCloseRequested();
    }

    Rectangle {
        width: parent.width
        visible: root.hasValidationErrors && root.validationMessage !== ""
        implicitHeight: errText.implicitHeight + Style.spacingS * 2
        radius: Style.cornerRadiusS
        color: Style.withAlpha(Style.errorContainer, 0.6)
        border.width: Style.outlineWidth
        border.color: Style.error

        StyledText {
            id: errText
            anchors.fill: parent
            anchors.margins: Style.spacingS
            text: root.validationMessage
            font.pixelSize: Style.fontSizeSmall
            color: Style.error
            wrapMode: Text.WordWrap
        }
    }

    leftActions: [
        DButton {
            text: I18n.tr("Cancel")
            variant: "text"
            onClicked: root.close()
        }
    ]

    actions: [
        DButton {
            text: I18n.tr("Discard")
            variant: "tonal"
            danger: true
            onClicked: {
                root.close();
                root.discardRequested();
            }
        },
        DButton {
            text: I18n.tr("Apply & Close")
            variant: "filled"
            enabled: !root.hasValidationErrors
            onClicked: {
                root.close();
                root.applyAndCloseRequested();
            }
        }
    ]
}
