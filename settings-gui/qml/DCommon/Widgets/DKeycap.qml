import QtQuick
import DCommon.Common

Rectangle {
    id: root

    property alias text: label.text
    property alias reserveText: label.reserveText
    property alias textColor: label.color
    property int textSize: Style.fontSizeMedium

    readonly property real bevelDepth: Style.outlineWidthFocused

    implicitWidth: Math.max(Math.ceil(label.reservedWidth) + Style.spacingM * 2, implicitHeight)
    implicitHeight: Math.round(label.font.pixelSize * 2)
    radius: Style.cornerRadiusXS
    color: Style.outlineVariant

    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: root.bevelDepth
        radius: root.radius
        color: Style.chipSurface
        border.width: Style.outlineWidth
        border.color: Style.outlineVariant

        NumericText {
            id: label
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.textSize
            font.weight: Style.fontWeightMedium
            color: Style.surfaceText
        }
    }
}
