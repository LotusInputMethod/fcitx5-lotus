import QtQuick
import DCommon.Common

Rectangle {
    anchors.fill: parent
    anchors.margins: -Style.focusRingOffset
    radius: Math.min(Style.fullRadius(width, height), (parent.radius !== undefined ? parent.radius : 0) + Style.focusRingOffset)
    color: "transparent"
    border.width: Style.focusRingWidth
    border.color: Style.focusRingColor
    visible: parent.activeFocus
}
