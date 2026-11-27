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

    maximumWidth: 440
    title: I18n.tr("Add Binding")
    supportingText: I18n.tr("Enter a trigger key and select its action:")
    acceptEnabled: canAdd

    readonly property string trimmedKey: keyField.text.trim()
    readonly property var actionLabels: {
        var list = Backend.bambooActions || []
        var out = []
        for (var i = 0; i < list.length; i++)
            out.push(String(list[i].label || ""))
        return out
    }
    readonly property var actionCodes: {
        var list = Backend.bambooActions || []
        var out = []
        for (var i = 0; i < list.length; i++)
            out.push(String(list[i].code || ""))
        return out
    }
    readonly property bool canAdd: trimmedKey.length > 0 && selectedActionCode.length > 0
    property string selectedActionCode: ""

    signal bindingAdded(string key, string actionCode)

    function defaultActionCode() {
        return actionCodes.length > 0 ? actionCodes[0] : ""
    }

    function open() {
        keyField.text = ""
        selectedActionCode = defaultActionCode()
        actionDropdown.currentValue = selectedActionCode
        opened = true
        keyField.forceActiveFocus()
    }

    function submit() {
        if (!canAdd)
            return
        bindingAdded(trimmedKey, selectedActionCode)
        close()
    }

    Component.onCompleted: selectedActionCode = defaultActionCode()

    onAccepted: submit()

    ColumnLayout {
        width: parent.width
        spacing: DC.Style.spacingM

        DW.DTextField {
            id: keyField
            Layout.fillWidth: true
            outlined: true
            labelText: I18n.tr("Trigger Key")
            placeholderText: I18n.tr("Key (e.g. s)")
            maximumLength: 1
            font.family: DC.Style.monoFontFamily
            onAccepted: root.submit()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingXS

            DW.StyledText {
                Layout.fillWidth: true
                text: I18n.tr("Action")
                font.pixelSize: DC.Style.fontSizeMedium
                font.weight: DC.Style.fontWeightMedium
                color: DC.Style.surfaceText
            }

            DW.DDropdown {
                id: actionDropdown
                Layout.fillWidth: true
                dropdownWidth: parent.width
                options: root.actionLabels
                optionValues: root.actionCodes
                currentValue: root.selectedActionCode
                onValueChanged: function(val) {
                    root.selectedActionCode = val
                }
            }
        }
    }

    actions: [
        DW.DButton {
            text: I18n.tr("Cancel")
            variant: "text"
            onClicked: root.close()
        },
        DW.DButton {
            text: I18n.tr("Add")
            enabled: root.canAdd
            onClicked: root.submit()
        }
    ]
}
