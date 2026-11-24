// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

OverlayDialog {
    id: root

    property string sectionId: ""
    property string sectionTitle: ""
    property var changes: []

    maximumWidth: 460
    title: root.sectionTitle !== ""
        ? I18n.tr("Restore Defaults — {}").replace("{}", root.sectionTitle)
        : I18n.tr("Restore All Defaults")
    supportingText: root.changes.length > 0
        ? I18n.tr("The following settings will be reset to their factory defaults:")
        : I18n.tr("Restore all modified settings to their factory defaults?")

    function openFor(id, titleText) {
        sectionId = id;
        sectionTitle = titleText || "";
        changes = Backend.getSectionResetChanges(id) || [];
        open();
    }

    function applyReset() {
        if (root.sectionId !== "")
            Backend.resetSection(root.sectionId);
        root.close();
    }

    onAccepted: applyReset()

    DCard {
        width: parent.width
        visible: root.changes.length > 0
        pad: Style.spacingS
        color: Style.surfaceContainerHigh
        implicitHeight: Math.min(260, changesCol.implicitHeight + pad * 2)

        DFlickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: changesCol.implicitHeight
            clip: true

            ColumnLayout {
                id: changesCol
                width: parent.width
                spacing: 0

                Repeater {
                    model: root.changes

                    ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 0

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.margins: Style.spacingS
                            spacing: Style.spacingXXS

                            StyledText {
                                Layout.fillWidth: true
                                text: String(modelData.label ?? "")
                                font.pixelSize: Style.fontSizeMedium
                                font.weight: Style.fontWeightMedium
                                color: Style.surfaceText
                                wrapMode: Text.WordWrap
                            }

                            StyledText {
                                Layout.fillWidth: true
                                text: String(modelData.current ?? "") + " → " + String(modelData.defaultVal ?? "")
                                font.pixelSize: Style.fontSizeSmall
                                color: Style.surfaceVariantText
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Style.withAlpha(Style.outlineVariant, 0.45)
                            visible: index < root.changes.length - 1
                        }
                    }
                }
            }
        }
    }

    actions: [
        DButton {
            text: I18n.tr("Cancel")
            variant: "text"
            onClicked: root.close()
        },
        DButton {
            text: I18n.tr("Reset")
            variant: "tonal"
            danger: true
            onClicked: root.applyReset()
        }
    ]
}
