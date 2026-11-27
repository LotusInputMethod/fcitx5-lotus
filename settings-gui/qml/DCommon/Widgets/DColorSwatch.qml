// Qt 6.2+ pure-QML color swatch circle (no ShaderEffect/.qsb)
import QtQuick
import DCommon.Common

Rectangle {
    id: root

    property color swatchColor: "transparent"
    property color secondaryColor: swatchColor
    property color tertiaryColor: swatchColor
    property color ringColor: Style.outline
    property real ringWidth: Style.outlineWidth
    property real minPreviewAlpha: 0.4

    radius: Math.min(width, height) / 2
    color: swatchColor
    border.width: ringWidth
    border.color: ringColor
}
