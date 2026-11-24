// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import "../DCommon/Widgets" as DW
import "../DCommon/Common" as DC
import "../components" as LC

LC.OverlayDialog {
    id: root

    acceptEnabled: false
    maximumWidth: 480
    maximumHeight: isEdit ? 420 : 360
    title: isEdit ? I18n.tr("Edit Macro") : I18n.tr("Add Macro")

    property bool isEdit: false
    property string originalKey: ""

    readonly property string trimmedKey: keyField.text.trim()
    readonly property string trimmedExpansion: expansionEdit.text.trim()
    readonly property bool keyInvalid: trimmedKey.length > 0 && !Backend.isValidMacroKey(trimmedKey)
    readonly property bool canSave: trimmedKey.length > 0 && trimmedExpansion.length > 0 && !keyInvalid

    signal saved(string oldKey, string newKey, string value)
    signal removed(string key)

    function openForAdd() {
        isEdit = false
        originalKey = ""
        keyField.text = ""
        expansionEdit.text = ""
        opened = true
        keyField.forceActiveFocus()
    }

    function openForEdit(key, value) {
        isEdit = true
        originalKey = key
        keyField.text = key
        expansionEdit.text = value
        opened = true
        keyField.forceActiveFocus()
    }

    function submit() {
        if (!canSave)
            return
        saved(originalKey, trimmedKey, trimmedExpansion)
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: DC.Style.spacingS

        DW.StyledText {
            Layout.fillWidth: true
            text: I18n.tr("Abbreviation")
            font.pixelSize: DC.Style.fontSizeMedium
            font.weight: DC.Style.fontWeightMedium
            color: DC.Style.surfaceText
        }

        DW.DTextField {
            id: keyField
            Layout.fillWidth: true
            outlined: true
            placeholderText: I18n.tr("e.g. vn, branch, cnow")
            font.family: DC.Style.monoFontFamily
            isError: root.keyInvalid
            supportingText: root.keyInvalid
                ? I18n.tr("Macro keys cannot contain spaces or special characters.")
                : ""
            onAccepted: {
                if (root.canSave)
                    root.submit()
                else
                    expansionEdit.forceActiveFocus()
            }
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: DC.Style.spacingS

        RowLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingXS

            DW.StyledText {
                Layout.fillWidth: true
                text: I18n.tr("Expansion")
                font.pixelSize: DC.Style.fontSizeMedium
                font.weight: DC.Style.fontWeightMedium
                color: DC.Style.surfaceText
            }

            DW.DButton {
                text: "+ $TIME"
                variant: "tonal"
                buttonHeight: DC.Style.buttonHeightXXS
                minimumWidth: 0
                tooltipText: I18n.tr("Insert $TIME dynamic placeholder")
                onClicked: {
                    expansionEdit.insertText("$TIME")
                    expansionEdit.forceActiveFocus()
                }
            }

            DW.DButton {
                text: "+ $DATE"
                variant: "tonal"
                buttonHeight: DC.Style.buttonHeightXXS
                minimumWidth: 0
                tooltipText: I18n.tr("Insert $DATE dynamic placeholder")
                onClicked: {
                    expansionEdit.insertText("$DATE")
                    expansionEdit.forceActiveFocus()
                }
            }
        }

        DW.DTextEdit {
            id: expansionEdit
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            placeholderText: I18n.tr("Expanded text...")
        }
    }

    leftActions: [
        DW.DButton {
            visible: root.isEdit
            text: I18n.tr("Remove")
            iconName: "delete"
            variant: "tonal"
            danger: true
            onClicked: {
                root.removed(root.originalKey)
                root.close()
            }
        }
    ]

    actions: [
        DW.DButton {
            text: I18n.tr("Cancel")
            variant: "text"
            onClicked: root.close()
        },
        DW.DButton {
            text: root.isEdit ? I18n.tr("Save") : I18n.tr("Add")
            enabled: root.canSave
            onClicked: root.submit()
        }
    ]
}
