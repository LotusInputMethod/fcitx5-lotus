// Qt 6.2+ pure-QML icon renderer using DIcon / image://theme
import QtQuick
import DCommon.Common

Item {
    id: root

    property string iconValue: ""
    property int iconSize: 24
    property string fallbackText: "A"
    property color iconColor: Style.surfaceText
    property color colorOverride: "transparent"
    property color fallbackBackgroundColor: Style.surfaceLight
    property real fallbackRadius: Style.cornerRadius
    property color fallbackTextColor: Style.primary
    property real materialIconSizeAdjustment: Style.spacingM
    property real unicodeIconScale: 0.7
    property real fallbackTextScale: 0.4
    property real iconMargins: 0
    property bool animate: false

    readonly property bool isMaterial: iconValue && iconValue.startsWith("material:")
    readonly property bool isUnicode: iconValue && iconValue.startsWith("unicode:")
    readonly property bool hasColorOverride: colorOverride.a > 0
    readonly property string materialName: isMaterial ? iconValue.substring(9) : ""
    readonly property string unicodeChar: isUnicode ? iconValue.substring(8) : ""

    visible: iconValue !== undefined && iconValue !== ""
    implicitWidth: iconSize
    implicitHeight: iconSize

    DIcon {
        anchors.centerIn: parent
        visible: root.isMaterial
        name: root.materialName
        size: root.iconSize - root.materialIconSizeAdjustment
        color: root.hasColorOverride ? root.colorOverride : root.iconColor
    }

    StyledText {
        anchors.centerIn: parent
        visible: root.isUnicode
        text: root.unicodeChar
        font.pixelSize: root.iconSize * root.unicodeIconScale
        color: root.hasColorOverride ? root.colorOverride : root.iconColor
    }

    Image {
        id: themeImg
        anchors.fill: parent
        anchors.margins: root.iconMargins
        visible: !root.isMaterial && !root.isUnicode && status === Image.Ready
        source: (!root.isMaterial && !root.isUnicode && root.iconValue) ? Paths.resolveIconPath(root.iconValue) : ""
        sourceSize: Qt.size(root.iconSize, root.iconSize)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.isMaterial && !root.isUnicode && themeImg.status !== Image.Ready
        color: root.fallbackBackgroundColor
        radius: root.fallbackRadius

        StyledText {
            anchors.centerIn: parent
            text: root.fallbackText
            font.pixelSize: root.iconSize * root.fallbackTextScale
            color: root.fallbackTextColor
            font.weight: Style.fontWeightMedium
        }
    }
}
