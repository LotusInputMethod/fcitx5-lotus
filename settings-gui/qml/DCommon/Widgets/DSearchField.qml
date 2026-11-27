import QtQuick
import DCommon.Common

DTextField {
    fieldHeight: Style.fieldHeightLarge
    cornerRadius: Style.pillControls ? Style.fullRadius(width, height) : Style.cornerRadiusL
    normalBorderColor: Style.outlineVariant
    focusedBorderColor: Style.focusRingWidth > 0 ? Style.focusRingColor : normalBorderColor
    borderWidth: Style.outlineWidth
    focusedBorderWidth: Math.max(borderWidth, Style.focusRingWidth)
    placeholderColor: Style.surfaceTextSecondary
    leftIconName: "search"
    hidePlaceholderOnFocus: false
    showClearButton: true
}
