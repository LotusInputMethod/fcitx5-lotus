import QtQuick
import DCommon.Common

StyledText {
    id: root

    property string reserveText: ""
    readonly property string reserveTemplate: (reserveText || text).replace(/\d/g, Fonts.widestDigit(font))
    readonly property real reservedWidth: Math.ceil(Math.max(implicitWidth, Fonts.textWidth(font, reserveTemplate)))

    width: reservedWidth
    wrapMode: Text.NoWrap
    horizontalAlignment: Text.AlignHCenter
}
