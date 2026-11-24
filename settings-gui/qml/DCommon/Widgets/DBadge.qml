import QtQuick
import DCommon.Common

Rectangle {
    id: root

    property alias text: label.text
    property alias textColor: label.color
    property real maximumWidth: Infinity
    property var tooltipText: null
    readonly property bool truncated: label.truncated

    implicitWidth: text ? Math.min(maximumWidth, Math.max(implicitHeight, Math.ceil(label.implicitWidth) + Style.spacingS * 2)) : implicitHeight
    implicitHeight: text ? Math.max(Style.spacingL + Style.spacingXS, Math.ceil(labelMetrics.tightBoundingRect.height) + Style.spacingXS * 2) : Style.spacingXS + Style.spacingXXS
    baselineOffset: label.y + label.baselineOffset
    radius: height / 2
    color: Style.primary
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    StyledTextMetrics {
        id: labelMetrics
        font: label.font
        text: label.text
    }

    StyledText {
        id: label
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round((root.height - labelMetrics.tightBoundingRect.height) / 2 - baselineOffset - labelMetrics.tightBoundingRect.y)
        width: Math.min(implicitWidth, Math.max(0, root.maximumWidth - Style.spacingS * 2))
        font.pixelSize: Style.fontSizeSmall
        font.weight: Style.fontWeightMedium
        color: Style.onPrimary
        wrapMode: Text.NoWrap
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        visible: text !== ""
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        enabled: root.tooltipText !== null && root.tooltipText !== ""
        hoverEnabled: enabled
        acceptedButtons: Qt.NoButton
    }

    DTooltipHost {
        text: root.tooltipText
        target: root
        hoverArea: hoverArea
    }
}
