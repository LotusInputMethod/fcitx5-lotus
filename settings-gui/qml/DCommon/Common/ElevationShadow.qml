// Qt 6.2+ compatible pure-QML elevation surface + drop shadow (no ShaderEffect/.qsb)
import QtQuick

Item {
    id: root

    property var level: Style.elevationLevel2
    property string direction: Style.elevationLightDirection ?? "bottom"
    property real fallbackOffset: 4

    property color targetColor: Style.surfaceContainer
    property real targetRadius: Style.cornerRadius
    property color borderColor: "transparent"
    property real borderWidth: 0

    property bool shadowEnabled: Style.elevationEnabled
    property real shadowBlurPx: level && level.blurPx !== undefined ? level.blurPx : 0
    property real shadowSpreadPx: level && level.spreadPx !== undefined ? level.spreadPx : 0
    property real shadowOffsetX: Style.elevationOffsetXFor(level, direction, fallbackOffset)
    property real shadowOffsetY: Style.elevationOffsetYFor(level, direction, fallbackOffset)
    property color shadowColor: Style.elevationShadowColor(level)
    property real shadowOpacity: 1

    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        anchors.topMargin: Math.max(-2, root.shadowOffsetY - 1)
        anchors.bottomMargin: -Math.max(2, root.shadowOffsetY + 2)
        radius: root.targetRadius + 2
        color: Style.withAlpha(root.shadowColor, (root.level?.alpha ?? 0.25) * 0.45 * root.shadowOpacity)
        visible: root.shadowEnabled
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: Math.max(0, root.shadowOffsetY * 0.5)
        anchors.bottomMargin: -Math.max(1, root.shadowOffsetY * 0.6)
        anchors.leftMargin: -1
        anchors.rightMargin: -1
        radius: root.targetRadius + 1
        color: Style.withAlpha(root.shadowColor, (root.level?.alpha ?? 0.25) * 0.35 * root.shadowOpacity)
        visible: root.shadowEnabled
    }

    Rectangle {
        anchors.fill: parent
        radius: root.targetRadius
        color: root.targetColor
        border.color: root.borderColor
        border.width: root.borderWidth
    }
}
