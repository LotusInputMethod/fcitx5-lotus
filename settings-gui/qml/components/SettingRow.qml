// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property string tooltipText: ""
    property bool isError: false
    property bool interactive: false
    property bool showSeparator: false
    property color backgroundColor: "transparent"
    readonly property var toggleChild: {
        for (let i = 0; i < controlBox.children.length; i++) {
            const child = controlBox.children[i];
            if (child && typeof child.handleClick === "function" && typeof child.toggled === "function")
                return child;
        }
        return null;
    }
    readonly property bool hasClickableToggle: toggleChild !== null && toggleChild.enabled && !toggleChild.toggling
    property color hoverBackgroundColor: (interactive || hasClickableToggle) ? Style.surfaceContainerHigh : Style.withAlpha(Style.surfaceText, 0.04)
    default property alias control: controlBox.data

    signal clicked

    Layout.fillWidth: true
    implicitHeight: Math.max(52, rowLayout.implicitHeight + Style.spacingM * 2)
    radius: Style.cornerRadiusS
    color: {
        if (isError)
            return Style.withAlpha(Style.errorContainer, 0.45);
        if (rowMouse.containsMouse)
            return hoverBackgroundColor;
        return backgroundColor;
    }
    border.width: isError ? Style.outlineWidth : 0
    border.color: isError ? Style.error : "transparent"

    Behavior on color {
        enabled: !Style.reduceMotion
        DColorAnim {
            duration: Style.expressiveDurations.expressiveFastEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveFastEffects
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: (root.interactive || root.hasClickableToggle) ? Qt.LeftButton : Qt.NoButton
        cursorShape: (root.interactive || root.hasClickableToggle) ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (root.hasClickableToggle)
                root.toggleChild.handleClick();
            else
                root.clicked();
        }
    }

    DTooltipHost {
        text: root.tooltipText !== "" ? root.tooltipText : null
        multiline: true
        target: root
        hoverArea: rowMouse
        side: "top"
    }

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        anchors.leftMargin: Style.spacingL
        anchors.rightMargin: Style.spacingL
        anchors.topMargin: Style.spacingS
        anchors.bottomMargin: Style.spacingS
        spacing: Style.spacingM

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Style.spacingXXS

            StyledText {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: Style.fontSizeMedium
                font.weight: Style.fontWeightMedium
                color: root.isError ? Style.error : (root.enabled ? Style.surfaceText : Style.onSurface_38)
                wrapMode: Text.WordWrap
            }

            StyledText {
                Layout.fillWidth: true
                visible: root.subtitle !== ""
                text: root.subtitle
                font.pixelSize: Style.fontSizeSmall
                color: root.isError ? Style.error : Style.surfaceVariantText
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            id: controlBox
            Layout.alignment: Qt.AlignVCenter
            spacing: Style.spacingS
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Style.spacingL
        anchors.rightMargin: Style.spacingL
        height: 1
        color: Style.withAlpha(Style.outlineVariant, 0.45)
        visible: root.showSeparator
    }
}
