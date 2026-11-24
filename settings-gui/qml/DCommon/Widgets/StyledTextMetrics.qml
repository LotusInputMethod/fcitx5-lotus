import QtQuick
import DCommon.Common

TextMetrics {
    property bool isMonospace: false
    property string fontToken: isMonospace ? "mono" : "ui"

    readonly property string resolvedFontFamily: Style.fontFor(fontToken)

    font.pixelSize: Appearance.fontSize.normal
    font.family: resolvedFontFamily
    font.weight: Style.fontWeightFor(fontToken)
}
