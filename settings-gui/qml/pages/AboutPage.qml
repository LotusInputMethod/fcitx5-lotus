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

    readonly property string repoUrl: "https://github.com/LotusInputMethod/fcitx5-lotus"
    readonly property string contributorsUrl: repoUrl + "/graphs/contributors"
    readonly property string bugUrl: repoUrl + "/issues/new?template=bug_report.yml"
    readonly property string featureUrl: repoUrl + "/issues/new?template=feature_request.yml"
    readonly property string blankIssueUrl: repoUrl + "/issues/new"

    property string debugStatusText: ""
    property bool debugStatusError: false
    property bool debugExportPending: false

    component ExternalLinkRow: ColumnLayout {
        id: linkItem
        required property string linkTitle
        required property string linkIcon
        required property string linkUrl
        property int titleWeight: DC.Style.fontWeight
        property bool showSeparator: false

        Layout.fillWidth: true
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.max(52, linkRowLayout.implicitHeight + DC.Style.spacingS * 2)
            radius: DC.Style.cornerRadiusS
            color: linkMouse.containsMouse ? DC.Style.surfaceContainerHigh : "transparent"

            MouseArea {
                id: linkMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally(linkItem.linkUrl)
            }

            DW.DTooltipHost {
                text: linkItem.linkUrl
                multiline: true
                target: parent
                hoverArea: linkMouse
                side: "top"
            }

            RowLayout {
                id: linkRowLayout
                anchors.fill: parent
                anchors.leftMargin: DC.Style.spacingL
                anchors.rightMargin: DC.Style.spacingL
                spacing: DC.Style.spacingM

                DW.DIcon {
                    name: linkItem.linkIcon
                    size: DC.Style.iconSizeMedium
                    color: DC.Style.primary
                }

                DW.StyledText {
                    Layout.fillWidth: true
                    text: linkItem.linkTitle
                    font.pixelSize: DC.Style.fontSizeMedium
                    font.weight: linkItem.titleWeight
                    color: DC.Style.surfaceText
                }

                DW.DIcon {
                    name: "open_in_new"
                    size: DC.Style.iconSizeMedium
                    color: DC.Style.onSurfaceVariant
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: DC.Style.spacingL
            Layout.rightMargin: DC.Style.spacingL
            Layout.preferredHeight: 1
            color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
            visible: linkItem.showSeparator
        }
    }

    Connections {
        target: typeof Backend !== "undefined" ? Backend : null
        ignoreUnknownSignals: true
        function onDebugExportFinished(ok, message) {
            if (!pageRoot.debugExportPending)
                return
            pageRoot.debugExportPending = false
            pageRoot.debugStatusError = !ok
            pageRoot.debugStatusText = String(message || "")
        }
    }

    FileDialog {
        id: debugFileDialog
        title: I18n.tr("Export Debug Information")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        nameFilters: [I18n.tr("Debug Log (*.log *.txt)"), I18n.tr("All Files (*)")]
        onAccepted: {
            pageRoot.debugExportPending = true
            pageRoot.debugStatusError = false
            pageRoot.debugStatusText = I18n.tr("Exporting debug information...")
            Backend.exportDebugInfo(selectedFile.toString())
        }
    }

    LC.SettingsPage {
        // Section 1: Identity & Links
        DW.DCard {
            Layout.fillWidth: true
            pad: DC.Style.spacingS

            ColumnLayout {
                width: parent.width
                spacing: DC.Style.spacingS

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: DC.Style.spacingL
                    Layout.rightMargin: DC.Style.spacingL
                    Layout.topMargin: DC.Style.spacingS
                    Layout.bottomMargin: DC.Style.spacingXS
                    spacing: DC.Style.spacingM

                    Item {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        Layout.alignment: Qt.AlignVCenter

                        Image {
                            id: appLogo
                            anchors.fill: parent
                            sourceSize.width: 48
                            sourceSize.height: 48
                            fillMode: Image.PreserveAspectFit
                            source: "image://theme/fcitx-lotus"
                            visible: status === Image.Ready
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: DC.Style.cornerRadiusM
                            color: DC.Style.primaryContainer
                            visible: !appLogo.visible

                            DW.DIcon {
                                anchors.centerIn: parent
                                name: "keyboard"
                                size: DC.Style.iconSizeLarge
                                color: DC.Style.onPrimaryContainer
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: DC.Style.spacingXXS

                        DW.StyledText {
                            Layout.fillWidth: true
                            text: "Fcitx5 Lotus"
                            font.pixelSize: DC.Style.fontSizeLarge
                            font.weight: DC.Style.fontWeightBold
                            color: DC.Style.surfaceText
                        }

                        DW.StyledText {
                            Layout.fillWidth: true
                            text: I18n.tr("Modern Vietnamese Input Method for Linux")
                            font.pixelSize: DC.Style.fontSizeSmall
                            color: DC.Style.onSurfaceVariant
                        }
                    }

                    DW.DBadge {
                        Layout.alignment: Qt.AlignVCenter
                        text: "v" + (Backend.appVersion || "dev")
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: DC.Style.spacingL
                    Layout.rightMargin: DC.Style.spacingL
                    Layout.preferredHeight: 1
                    color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Repeater {
                        model: [
                            { title: I18n.tr("GitHub Repository"), icon: "code", url: pageRoot.repoUrl },
                            { title: I18n.tr("Contributors"), icon: "group", url: pageRoot.contributorsUrl }
                        ]

                        ExternalLinkRow {
                            required property var modelData
                            required property int index
                            linkTitle: modelData.title
                            linkIcon: modelData.icon
                            linkUrl: modelData.url
                            showSeparator: index === 0
                        }
                    }
                }
            }
        }

        // Section 2: Report an Issue
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Report an Issue")
                sectionId: ""
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    Repeater {
                        model: [
                            { title: I18n.tr("Bug Report"), icon: "warning", url: pageRoot.bugUrl },
                            { title: I18n.tr("Feature Request"), icon: "info", url: pageRoot.featureUrl },
                            { title: I18n.tr("Blank Issue / Question"), icon: "edit_note", url: pageRoot.blankIssueUrl }
                        ]

                        ExternalLinkRow {
                            required property var modelData
                            required property int index
                            linkTitle: modelData.title
                            linkIcon: modelData.icon
                            linkUrl: modelData.url
                            titleWeight: DC.Style.fontWeightMedium
                            showSeparator: index < 2
                        }
                    }
                }
            }
        }

        // Section 3: Diagnostics
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Diagnostics")
                sectionId: ""
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    width: parent.width
                    spacing: DC.Style.spacingXS

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: I18n.tr("Export Debug Information")
                        subtitle: I18n.tr("Save system details, Lotus config, uinput logs, and fcitx5-diagnose output")

                        DW.DButton {
                            text: I18n.tr("Export…")
                            iconName: "save"
                            variant: "tonal"
                            onClicked: debugFileDialog.open()
                        }
                    }

                    DW.StyledText {
                        Layout.fillWidth: true
                        Layout.leftMargin: DC.Style.spacingL
                        Layout.rightMargin: DC.Style.spacingL
                        Layout.bottomMargin: DC.Style.spacingS
                        visible: pageRoot.debugStatusText !== ""
                        text: pageRoot.debugStatusText
                        font.pixelSize: DC.Style.fontSizeSmall
                        color: pageRoot.debugStatusError ? DC.Style.error : DC.Style.primary
                    }
                }
            }
        }

        // Section 4: Licenses
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Licenses")
                sectionId: ""
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    Repeater {
                        model: [
                            { name: "Fcitx5 Lotus", spdx: "GPL-3.0-or-later" },
                            { name: "Bamboo Core Engine", spdx: "MIT" },
                            { name: "fcitx5-bamboo", spdx: "LGPL-2.1-or-later" },
                            { name: "dux-qml-common (DCommon)", spdx: "MIT" }
                        ]

                        ColumnLayout {
                            id: licRow
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: DC.Style.spacingL
                                Layout.rightMargin: DC.Style.spacingL
                                Layout.topMargin: DC.Style.spacingM
                                Layout.bottomMargin: DC.Style.spacingM
                                spacing: DC.Style.spacingM

                                DW.StyledText {
                                    Layout.fillWidth: true
                                    text: licRow.modelData.name
                                    font.pixelSize: DC.Style.fontSizeMedium
                                    color: DC.Style.surfaceText
                                }

                                DW.StyledText {
                                    text: licRow.modelData.spdx
                                    font.pixelSize: DC.Style.fontSizeSmall
                                    color: DC.Style.onSurfaceVariant
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: DC.Style.spacingL
                                Layout.rightMargin: DC.Style.spacingL
                                Layout.preferredHeight: 1
                                color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                                visible: licRow.index < 3
                            }
                        }
                    }
                }
            }
        }
    }
}
