// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

Item {
    id: root

    property string title: ""
    property string sectionId: ""
    property bool autoResetDialog: true
    default property alias trailing: trailingBox.data

    property int _rev: 0

    signal resetClicked

    function refresh() {
        _rev++;
    }

    readonly property bool modifiedFromDefault: {
        const _r = _rev;
        const _v = typeof Backend !== "undefined" ? Backend.values : null;
        const _m = typeof Backend !== "undefined" ? Backend.modeList : null;
        const _u = typeof Backend !== "undefined" ? Backend.hasUnsavedChanges : false;
        if (sectionId === "" || typeof Backend === "undefined")
            return false;
        return Backend.isSectionModifiedFromDefault(sectionId);
    }

    Connections {
        target: typeof Backend !== "undefined" ? Backend : null
        ignoreUnknownSignals: true
        function onStateChanged() { root._rev++; }
        function onValuesChanged() { root._rev++; }
        function onModeListChanged() { root._rev++; }
    }

    implicitHeight: Math.max(32, headerRow.implicitHeight)
    Layout.fillWidth: true

    RowLayout {
        id: headerRow
        anchors.fill: parent
        spacing: Style.spacingS

        StyledText {
            Layout.fillWidth: true
            text: root.title
            font.pixelSize: Style.fontSizeMedium
            font.weight: Style.fontWeightBold
            color: Style.surfaceText
            elide: Text.ElideRight
        }

        DIconButton {
            id: resetBtn
            visible: root.modifiedFromDefault
            iconName: "edit-undo-symbolic"
            iconSize: Style.iconSizeSmall
            buttonSize: Style.buttonHeightXS
            tooltipText: I18n.tr("Restore section defaults")
            onClicked: {
                root.resetClicked();
                if (root.autoResetDialog && root.sectionId !== "")
                    resetDialog.openFor(root.sectionId, root.title);
            }
        }

        RowLayout {
            id: trailingBox
            spacing: Style.spacingS
            Layout.alignment: Qt.AlignVCenter
        }
    }

    SectionResetDialog {
        id: resetDialog
    }
}
