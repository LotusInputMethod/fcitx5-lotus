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
    property string selectedPreset: ""

    readonly property var presetNames: {
        var presets = Backend.keymapPresets || []
        var out = []
        for (var i = 0; i < presets.length; i++)
            out.push(String(presets[i].name || ""))
        return out
    }

    readonly property var actionLabels: {
        var actions = Backend.bambooActions || []
        var out = []
        for (var i = 0; i < actions.length; i++)
            out.push(String(actions[i].label || ""))
        return out
    }

    readonly property var actionCodes: {
        var actions = Backend.bambooActions || []
        var out = []
        for (var i = 0; i < actions.length; i++)
            out.push(String(actions[i].code || ""))
        return out
    }

    readonly property var filteredBindings: {
        var bindings = Backend.keymapBindings || []
        var q = searchQuery.trim().toLowerCase()
        if (!q)
            return bindings
        var out = []
        for (var i = 0; i < bindings.length; i++) {
            var km = bindings[i]
            var kStr = String(km.key || "").toLowerCase()
            var codeStr = String(km.value || "").toLowerCase()
            var labelStr = actionLabelFor(km.value).toLowerCase()
            if (kStr.indexOf(q) !== -1 || codeStr.indexOf(q) !== -1 || labelStr.indexOf(q) !== -1)
                out.push(km)
        }
        return out
    }

    ListModel {
        id: bindingsModel
    }

    function syncBindingsModel() {
        ListModelSync.syncByKey(
            bindingsModel,
            filteredBindings,
            function(b) { return String(b.key || "") },
            function(b) {
                return {
                    keyText: String(b.key || ""),
                    actionCode: String(b.value || "")
                }
            }
        )
    }

    onFilteredBindingsChanged: syncBindingsModel()

    function boolSetting(key, fallback) {
        if (!Backend.values || Backend.values[key] === undefined)
            return fallback
        return Boolean(Backend.values[key])
    }

    function actionLabelFor(code) {
        var idx = actionCodes.indexOf(String(code || ""))
        if (idx >= 0)
            return actionLabels[idx]
        return String(code || "")
    }

    function syncDefaultPreset() {
        if (selectedPreset === "" && presetNames.length > 0)
            selectedPreset = presetNames[0]
    }

    Component.onCompleted: {
        syncBindingsModel()
        syncDefaultPreset()
    }
    onPresetNamesChanged: syncDefaultPreset()

    LC.SettingsPage {
        // Section 1: Configuration
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Configuration")
                sectionId: "keymap_config"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: configCol
                    width: parent.width
                    spacing: 2

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: I18n.tr("Custom Keymap")
                        tooltipText: I18n.tr("Use custom key-to-action bindings when Input Method is Custom")
                        showSeparator: true

                        DW.DToggle {
                            checked: pageRoot.boolSetting("EnableCustomKeymap", false)
                            onToggled: function(val) {
                                Backend.setValue("EnableCustomKeymap", val)
                            }
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: I18n.tr("Preset")
                        tooltipText: I18n.tr("Load bindings from a built-in layout")

                        RowLayout {
                            spacing: DC.Style.spacingS

                            DW.DDropdown {
                                id: presetDropdown
                                dropdownWidth: 200
                                Layout.preferredWidth: 200
                                options: pageRoot.presetNames
                                optionValues: pageRoot.presetNames
                                currentValue: pageRoot.selectedPreset
                                onValueChanged: function(val) {
                                    pageRoot.selectedPreset = val
                                }
                            }

                            DW.DButton {
                                text: I18n.tr("Apply")
                                variant: "tonal"
                                enabled: pageRoot.selectedPreset !== ""
                                tooltipText: I18n.tr("Replace bindings with the selected preset")
                                onClicked: {
                                    if (!Backend.keymapBindings || Backend.keymapBindings.length === 0) {
                                        Backend.applyKeymapPreset(pageRoot.selectedPreset)
                                    } else {
                                        presetConfirmDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Section 2: Bindings
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Bindings")
                sectionId: "keymap_bindings"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: bindingsCol
                    width: parent.width
                    spacing: DC.Style.spacingS

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: DC.Style.spacingS

                            DW.DSearchField {
                                Layout.fillWidth: true
                                placeholderText: I18n.tr("Search keys...")
                                onTextEdited: pageRoot.searchQuery = text
                            }

                            DW.DButton {
                                text: I18n.tr("Add")
                                iconName: "add"
                                tooltipText: I18n.tr("Add Binding")
                                onClicked: addKeymapDialog.open()
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
                            visible: pageRoot.filteredBindings.length === 0
                            color: DC.Style.onSurfaceVariant
                            text: (Backend.keymapBindings && Backend.keymapBindings.length > 0 && pageRoot.searchQuery.trim().length > 0)
                                ? I18n.tr("No Matching Bindings")
                                : I18n.tr("No Bindings")
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            visible: bindingsModel.count > 0

                            Repeater {
                                model: bindingsModel

                                ColumnLayout {
                                    id: bindingRow
                                    required property string keyText
                                    required property string actionCode
                                    required property int index

                                    Layout.fillWidth: true
                                    spacing: 0

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Math.max(DC.Style.listItemHeight, bindingContentRow.implicitHeight + DC.Style.spacingS * 2)
                                        radius: DC.Style.cornerRadiusS
                                        color: bindingHover.hovered ? DC.Style.withAlpha(DC.Style.surfaceText, 0.04) : "transparent"

                                        HoverHandler {
                                            id: bindingHover
                                        }

                                        RowLayout {
                                            id: bindingContentRow
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.leftMargin: DC.Style.spacingS
                                            anchors.rightMargin: DC.Style.spacingS
                                            spacing: DC.Style.spacingM

                                            DW.DKeycap {
                                                Layout.alignment: Qt.AlignVCenter
                                                Layout.minimumWidth: 36
                                                text: bindingRow.keyText
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                            }

                                            DW.DDropdown {
                                                id: rowActionDropdown
                                                Layout.alignment: Qt.AlignVCenter
                                                dropdownWidth: 230
                                                Layout.preferredWidth: 230
                                                options: pageRoot.actionLabels
                                                optionValues: pageRoot.actionCodes
                                                onValueChanged: function(val) {
                                                    Backend.upsertKeymap(bindingRow.keyText, val)
                                                }

                                                Binding {
                                                    target: rowActionDropdown
                                                    property: "currentValue"
                                                    value: bindingRow.actionCode
                                                }
                                            }

                                            DW.DIconButton {
                                                Layout.alignment: Qt.AlignVCenter
                                                iconName: "delete"
                                                iconColor: DC.Style.error
                                                tooltipText: I18n.tr("Remove")
                                                onClicked: Backend.removeKeymap(bindingRow.keyText)
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 2
                                        Layout.preferredHeight: 1
                                        color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                                        visible: bindingRow.index < bindingsModel.count - 1
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    LD.AddKeymapDialog {
        id: addKeymapDialog
        onBindingAdded: function(key, actionCode) {
            Backend.upsertKeymap(key, actionCode)
        }
    }

    LC.OverlayDialog {
        id: presetConfirmDialog

        maximumWidth: 420
        title: I18n.tr("Apply Preset?")
        supportingText: I18n.tr("This operation will replace all existing keys with %1. Are you sure?").arg(pageRoot.selectedPreset)

        onAccepted: {
            Backend.applyKeymapPreset(pageRoot.selectedPreset)
            close()
        }

        actions: [
            DW.DButton {
                text: I18n.tr("Cancel")
                variant: "text"
                onClicked: presetConfirmDialog.close()
            },
            DW.DButton {
                text: I18n.tr("Apply")
                variant: "tonal"
                danger: true
                onClicked: presetConfirmDialog.accepted()
            }
        ]
    }
}
