// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import "../DCommon/Widgets" as DW
import "../DCommon/Common" as DC
import "../components" as LC
import "../components/ListModelSync.js" as ListModelSync
import "../dialogs" as LD

Item {
    id: pageRoot

    property string searchQuery: ""

    readonly property var skipValues: ["Disabled", "Shift", "Ctrl", "Alt"]
    readonly property var skipLabels: [
        I18n.tr("Disabled"),
        I18n.tr("Shift"),
        I18n.tr("Ctrl"),
        I18n.tr("Alt")
    ]

    readonly property var timeValues: ["%H:%M", "%H:%M:%S", "%I:%M %p", "%I:%M:%S %p", ""]
    readonly property var timeLabels: [
        "15:04 (24h)",
        "15:04:05 (24h)",
        "03:04 PM",
        "03:04:05 PM",
        I18n.tr("None")
    ]

    readonly property var dateValues: ["%d/%m/%Y", "%d/%m/%y", "%m/%d/%Y", "%Y-%m-%d", "%y-%m-%d", ""]
    readonly property var dateLabels: [
        "dd/MM/yyyy",
        "dd/MM/yy",
        "MM/dd/yyyy",
        "yyyy-MM-dd",
        "yy-MM-dd",
        I18n.tr("None")
    ]

    readonly property var filteredMacros: {
        var items = Backend.macros || []
        var q = searchQuery.trim().toLowerCase()
        if (!q)
            return items
        var out = []
        for (var i = 0; i < items.length; i++) {
            var m = items[i]
            var keyStr = String(m.key || "").toLowerCase()
            var valStr = String(m.value || "").toLowerCase()
            if (keyStr.indexOf(q) !== -1 || valStr.indexOf(q) !== -1)
                out.push(m)
        }
        return out
    }

    ListModel {
        id: macrosModel
    }

    function syncMacrosModel() {
        ListModelSync.syncByKey(
            macrosModel,
            filteredMacros,
            function(m) { return String(m.key || "") },
            function(m) {
                return {
                    keyText: String(m.key || ""),
                    valueText: String(m.value || ""),
                    invalidKey: Boolean(m.invalid)
                }
            }
        )
    }

    onFilteredMacrosChanged: syncMacrosModel()
    Component.onCompleted: syncMacrosModel()

    function boolSetting(key, fallback) {
        if (!Backend.values || Backend.values[key] === undefined)
            return fallback
        return Boolean(Backend.values[key])
    }

    function stringSetting(key, fallback) {
        if (!Backend.values || Backend.values[key] === undefined)
            return fallback
        return String(Backend.values[key])
    }

    function formatPreview(val) {
        var text = String(val || "")
        if (text.indexOf("\n") === -1)
            return text
        var lines = text.split("\n")
        for (var i = 0; i < lines.length; i++)
            lines[i] = lines[i].trim()
        return lines.join(" ⏎ ")
    }

    LC.SettingsPage {
        // Section 1: Behavior
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Behavior")
                sectionId: "macro_behavior"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: behaviorCol
                    width: parent.width
                    spacing: 2

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: Backend.optionTitle("EnableMacro")
                        tooltipText: I18n.tr("Expand shorthand abbreviations while typing")
                        showSeparator: true

                        DW.DToggle {
                            checked: pageRoot.boolSetting("EnableMacro", true)
                            onToggled: function(val) {
                                Backend.setValue("EnableMacro", val)
                            }
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: Backend.optionTitle("CapitalizeMacro")
                        tooltipText: I18n.tr("Automatically match expansion case to trigger key case.\n\nExample if 'kg' is 'Khô gà':\n- kg -> khô gà\n- Kg -> Khô gà\n- KG -> KHÔ GÀ")
                        showSeparator: true

                        DW.DToggle {
                            checked: pageRoot.boolSetting("CapitalizeMacro", true)
                            onToggled: function(val) {
                                Backend.setValue("CapitalizeMacro", val)
                            }
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: Backend.optionTitle("EnableMacroInOffMode")
                        tooltipText: I18n.tr("Allow macros to work when the input mode is OFF.\nWhen disabled, macros are only available in active typing modes.")
                        showSeparator: true

                        DW.DToggle {
                            checked: pageRoot.boolSetting("EnableMacroInOffMode", false)
                            onToggled: function(val) {
                                Backend.setValue("EnableMacroInOffMode", val)
                            }
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: Backend.optionTitle("MacroSkipTriggerModifier")
                        tooltipText: I18n.tr("Press and release the selected modifier key before typing to skip macro expansion for the next word.\nIf another key is pressed between the modifier's press and release, the skip is cancelled.")
                        showSeparator: true

                        DW.DDropdown {
                            id: skipDropdown
                            dropdownWidth: 180
                            Layout.preferredWidth: 180
                            options: pageRoot.skipLabels
                            optionValues: pageRoot.skipValues
                            onValueChanged: function(val) {
                                Backend.setValue("MacroSkipTriggerModifier", val)
                            }

                            Binding {
                                target: skipDropdown
                                property: "currentValue"
                                value: pageRoot.stringSetting("MacroSkipTriggerModifier", String(Backend.optionDefault("MacroSkipTriggerModifier")))
                            }
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: Backend.optionTitle("TimeFormat")
                        tooltipText: I18n.tr("Format used for the $TIME dynamic placeholder")
                        showSeparator: true

                        DW.DDropdown {
                            id: timeDropdown
                            dropdownWidth: 180
                            Layout.preferredWidth: 180
                            options: pageRoot.timeLabels
                            optionValues: pageRoot.timeValues
                            onValueChanged: function(val) {
                                Backend.setValue("TimeFormat", val)
                            }

                            Binding {
                                target: timeDropdown
                                property: "currentValue"
                                value: pageRoot.stringSetting("TimeFormat", String(Backend.optionDefault("TimeFormat")))
                            }
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: Backend.optionTitle("DateFormat")
                        tooltipText: I18n.tr("Format used for the $DATE dynamic placeholder")

                        DW.DDropdown {
                            id: dateDropdown
                            dropdownWidth: 180
                            Layout.preferredWidth: 180
                            options: pageRoot.dateLabels
                            optionValues: pageRoot.dateValues
                            onValueChanged: function(val) {
                                Backend.setValue("DateFormat", val)
                            }

                            Binding {
                                target: dateDropdown
                                property: "currentValue"
                                value: pageRoot.stringSetting("DateFormat", String(Backend.optionDefault("DateFormat")))
                            }
                        }
                    }
                }
            }
        }

        // Section 2: Macros List
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Macros")
                sectionId: "macro_list"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: macrosCol
                    width: parent.width
                    spacing: DC.Style.spacingS

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: DC.Style.spacingS

                        DW.DSearchField {
                            Layout.fillWidth: true
                            placeholderText: I18n.tr("Search macros...")
                            onTextEdited: pageRoot.searchQuery = text
                        }

                        DW.DButton {
                            text: I18n.tr("Add")
                            iconName: "add"
                            tooltipText: I18n.tr("Add Macro")
                            onClicked: macroEditDialog.openForAdd()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                    }

                    DW.StyledText {
                        Layout.fillWidth: true
                        Layout.topMargin: DC.Style.spacingM
                        Layout.bottomMargin: DC.Style.spacingM
                        horizontalAlignment: Text.AlignHCenter
                        visible: pageRoot.filteredMacros.length === 0
                        color: DC.Style.onSurfaceVariant
                        text: (Backend.macros && Backend.macros.length > 0 && pageRoot.searchQuery.trim().length > 0)
                            ? I18n.tr("No Matching Macros")
                            : I18n.tr("No Macros Defined")
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        visible: macrosModel.count > 0

                        Repeater {
                            model: macrosModel

                            ColumnLayout {
                                id: macroRow
                                required property string keyText
                                required property string valueText
                                required property bool invalidKey
                                required property int index

                                Layout.fillWidth: true
                                spacing: 0

                                Rectangle {
                                    id: macroTile
                                    Layout.fillWidth: true
                                    implicitHeight: Math.max(DC.Style.listItemHeight, macroRowContent.implicitHeight + DC.Style.spacingS * 2)
                                    radius: DC.Style.cornerRadiusS
                                    color: macroRow.invalidKey
                                        ? DC.Style.withAlpha(DC.Style.errorContainer, macroMouse.containsMouse ? 0.65 : 0.45)
                                        : (macroMouse.containsMouse ? DC.Style.withAlpha(DC.Style.surfaceText, 0.04) : "transparent")

                                    MouseArea {
                                        id: macroMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: macroEditDialog.openForEdit(macroRow.keyText, macroRow.valueText)
                                    }

                                    DW.DTooltipHost {
                                        text: macroRow.invalidKey
                                            ? I18n.tr("Macro keys cannot contain spaces or special characters.")
                                            : ((macroRow.valueText.length > 50 || macroRow.valueText.indexOf("\n") !== -1) ? (macroRow.keyText + " → " + macroRow.valueText) : null)
                                        multiline: !macroRow.invalidKey && macroRow.valueText.indexOf("\n") !== -1
                                        target: macroTile
                                        hoverArea: macroMouse
                                        side: "top"
                                    }

                                    RowLayout {
                                        id: macroRowContent
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: DC.Style.spacingS
                                        anchors.rightMargin: DC.Style.spacingS
                                        spacing: DC.Style.spacingM

                                        Item {
                                            Layout.preferredWidth: 140
                                            Layout.alignment: Qt.AlignVCenter
                                            implicitHeight: macroKeycap.implicitHeight

                                            DW.DKeycap {
                                                id: macroKeycap
                                                anchors.left: parent.left
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: Math.min(implicitWidth, parent.width)
                                                clip: true
                                                text: macroRow.keyText.length > 16 ? (macroRow.keyText.slice(0, 14) + "…") : macroRow.keyText
                                                textColor: macroRow.invalidKey ? DC.Style.error : DC.Style.surfaceText
                                            }
                                        }

                                        DW.StyledText {
                                            Layout.fillWidth: true
                                            Layout.alignment: Qt.AlignVCenter
                                            text: pageRoot.formatPreview(macroRow.valueText)
                                            font.pixelSize: DC.Style.fontSizeMedium
                                            color: macroRow.invalidKey ? DC.Style.onErrorContainer : DC.Style.surfaceText
                                            maximumLineCount: 2
                                            wrapMode: Text.WordWrap
                                            elide: Text.ElideRight
                                        }

                                        DW.DIcon {
                                            Layout.alignment: Qt.AlignVCenter
                                            name: "chevron_right"
                                            size: DC.Style.iconSizeMedium
                                            color: DC.Style.onSurfaceVariant
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 2
                                    Layout.preferredHeight: 1
                                    color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                                    visible: macroRow.index < macrosModel.count - 1
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    LD.MacroEditDialog {
        id: macroEditDialog
        onSaved: function(oldKey, newKey, value) {
            Backend.upsertMacro(oldKey, newKey, value)
        }
        onRemoved: function(key) {
            Backend.removeMacro(key)
        }
    }
}
