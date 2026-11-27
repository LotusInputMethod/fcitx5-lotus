// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import "../DCommon/Widgets" as DW
import "../DCommon/Common" as DC
import "../components" as LC

Item {
    id: pageRoot

    property var componentSelection: ({
        "config": true,
        "rules": true,
        "macros": true,
        "keymaps": true,
        "dictionary": true
    })

    readonly property var componentDefs: [
        {
            key: "config",
            title: I18n.tr("General & Typing"),
            tooltip: I18n.tr("Main input method, typing rules, shortcuts, and appearance")
        },
        {
            key: "rules",
            title: I18n.tr("Application Rules"),
            tooltip: I18n.tr("Per-application input modes and timing overrides")
        },
        {
            key: "macros",
            title: I18n.tr("Macros"),
            tooltip: I18n.tr("Macro behavior and shorthand expansions")
        },
        {
            key: "keymaps",
            title: I18n.tr("Custom Keymap"),
            tooltip: I18n.tr("Custom key-to-action mappings")
        },
        {
            key: "dictionary",
            title: I18n.tr("Custom Dictionary"),
            tooltip: I18n.tr("Custom excluded dictionary words")
        }
    ]

    property string statusMessage: ""
    property bool statusIsError: false
    property url pendingImportUrl: ""
    property var pendingImportMap: ({})
    property var pendingImportLabels: []

    function hasAnySelected() {
        for (var i = 0; i < componentDefs.length; i++) {
            if (componentSelection[componentDefs[i].key])
                return true
        }
        return false
    }

    function setComponentSelected(key, enabled) {
        var next = {}
        for (var k in componentSelection)
            next[k] = componentSelection[k]
        next[key] = enabled
        componentSelection = next
    }

    function showStatus(msg, isErr) {
        statusMessage = msg
        statusIsError = isErr
    }

    FileDialog {
        id: exportFileDialog
        title: I18n.tr("Export Backup")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [I18n.tr("JSON Backup (*.json)"), I18n.tr("All Files (*)")]
        onAccepted: {
            var ok = Backend.exportBackup(selectedFile.toString(), pageRoot.componentSelection)
            if (ok)
                pageRoot.showStatus(I18n.tr("Backup exported"), false)
            else
                pageRoot.showStatus(I18n.tr("Export failed"), true)
        }
    }

    FileDialog {
        id: importFileDialog
        title: I18n.tr("Import Backup")
        fileMode: FileDialog.OpenFile
        nameFilters: [I18n.tr("JSON Backup (*.json)"), I18n.tr("All Files (*)")]
        onAccepted: {
            var info = Backend.inspectBackup(selectedFile.toString())
            if (!info || !info.valid) {
                var errMsg = (info && info.error) ? String(info.error) : I18n.tr("Invalid backup file")
                pageRoot.showStatus(errMsg, true)
                return
            }
            var available = info.components || []
            var matchedMap = {}
            var matchedLabels = []
            for (var i = 0; i < pageRoot.componentDefs.length; i++) {
                var def = pageRoot.componentDefs[i]
                var inBackup = available.indexOf(def.key) !== -1
                var selected = Boolean(pageRoot.componentSelection[def.key])
                matchedMap[def.key] = (selected && inBackup)
                if (selected && inBackup)
                    matchedLabels.push(def.title)
            }
            if (matchedLabels.length === 0) {
                pageRoot.showStatus(I18n.tr("No matching components found in backup"), true)
                return
            }
            pageRoot.pendingImportUrl = selectedFile
            pageRoot.pendingImportMap = matchedMap
            pageRoot.pendingImportLabels = matchedLabels
            restoreConfirmDialog.open()
        }
    }

    LC.SettingsPage {
        // Section 1: Components
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Components")
                sectionId: ""
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: componentsCol
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: pageRoot.componentDefs

                        LC.SettingRow {
                            id: compRow
                            required property var modelData
                            required property int index

                            Layout.fillWidth: true
                            title: modelData.title
                            tooltipText: modelData.tooltip
                            showSeparator: index < pageRoot.componentDefs.length - 1

                            DW.DToggle {
                                checked: Boolean(pageRoot.componentSelection[compRow.modelData.key])
                                onToggled: function(val) {
                                    pageRoot.setComponentSelected(compRow.modelData.key, val)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Section 2: Bundle
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Bundle")
                sectionId: ""
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: bundleCol
                    width: parent.width
                    spacing: 2

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: I18n.tr("Export…")
                        subtitle: I18n.tr("Save selected components to a JSON backup file")
                        interactive: true
                        showSeparator: true
                        onClicked: {
                            if (!pageRoot.hasAnySelected()) {
                                pageRoot.showStatus(I18n.tr("Select at least one component to export"), true)
                                return
                            }
                            exportFileDialog.open()
                        }

                        DW.DIcon {
                            name: "save"
                            size: DC.Style.iconSizeMedium
                            color: DC.Style.onSurfaceVariant
                        }
                    }

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: I18n.tr("Import…")
                        subtitle: I18n.tr("Restore selected components from a JSON backup file")
                        interactive: true
                        showSeparator: false
                        onClicked: {
                            if (!pageRoot.hasAnySelected()) {
                                pageRoot.showStatus(I18n.tr("Select at least one component to import"), true)
                                return
                            }
                            importFileDialog.open()
                        }

                        DW.DIcon {
                            name: "folder_open"
                            size: DC.Style.iconSizeMedium
                            color: DC.Style.onSurfaceVariant
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: pageRoot.statusMessage !== ""
                implicitHeight: statusTextItem.implicitHeight + DC.Style.spacingM * 2
                radius: DC.Style.cornerRadiusM
                color: pageRoot.statusIsError ? DC.Style.errorContainer : DC.Style.secondaryContainer

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: DC.Style.spacingM
                    anchors.rightMargin: DC.Style.spacingM
                    spacing: DC.Style.spacingS

                    DW.DIcon {
                        name: pageRoot.statusIsError ? "warning" : "check"
                        size: DC.Style.iconSizeMedium
                        color: pageRoot.statusIsError ? DC.Style.onErrorContainer : DC.Style.onSecondaryContainer
                    }

                    DW.StyledText {
                        id: statusTextItem
                        Layout.fillWidth: true
                        text: pageRoot.statusMessage
                        font.pixelSize: DC.Style.fontSizeSmall
                        color: pageRoot.statusIsError ? DC.Style.onErrorContainer : DC.Style.onSecondaryContainer
                    }
                }
            }
        }
    }

    LC.OverlayDialog {
        id: restoreConfirmDialog

        maximumWidth: 440
        title: I18n.tr("Restore Backup?")
        supportingText: I18n.tr("Overwrite selected components: %1?").arg(pageRoot.pendingImportLabels.join(", "))

        onAccepted: {
            var ok = Backend.importBackup(pageRoot.pendingImportUrl.toString(), pageRoot.pendingImportMap)
            if (ok)
                pageRoot.showStatus(I18n.tr("Backup restored"), false)
            else
                pageRoot.showStatus(I18n.tr("Import failed"), true)
            close()
        }

        actions: [
            DW.DButton {
                text: I18n.tr("Cancel")
                variant: "text"
                onClicked: restoreConfirmDialog.close()
            },
            DW.DButton {
                text: I18n.tr("Restore")
                variant: "tonal"
                danger: true
                onClicked: restoreConfirmDialog.accepted()
            }
        ]
    }
}
