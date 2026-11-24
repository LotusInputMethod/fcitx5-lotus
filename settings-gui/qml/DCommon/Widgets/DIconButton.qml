import QtQuick
import DCommon.Common

DActionButton {
    id: root

    property string size: "s"
    property string widthMode: "default"
    property string variant: "standard"
    property bool round: true
    property color containerColor: variant === "filled" ? Style.primary : Style.secondaryContainer
    property color contentColor: variant === "filled" ? Style.onPrimary : Style.onSecondaryContainer

    readonly property bool medium: size === "m"
    readonly property bool filled: variant === "filled" || variant === "tonal" || (checkable && checked)
    readonly property real squareRadius: Style.buttonRadius(width, height, buttonSize, false, false)
    readonly property real roundRadius: Style.buttonRadius(width, height, buttonSize, false, true)
    readonly property real horizontalInset: {
        switch (widthMode) {
        case "narrow":
            return medium ? 12 : 4;
        case "wide":
            return medium ? 24 : 14;
        }
        return medium ? 16 : 8;
    }

    buttonSize: medium ? Style.buttonHeightM : Style.buttonHeightS
    width: iconSize + horizontalInset * 2
    iconSize: Style.iconSize
    radius: {
        if (!Style.shapeMorph)
            return round ? roundRadius : squareRadius;
        if (pressed)
            return Style.buttonRadius(width, height, buttonSize, true, false);
        return round !== (checkable && checked) ? roundRadius : squareRadius;
    }
    iconColor: filled ? contentColor : Style.onSurfaceVariant
    iconFilled: checkable && checked
    backgroundColor: !enabled && filled ? Style.onSurface_10 : (filled ? containerColor : "transparent")
    border.width: variant === "outlined" && !checked ? Style.outlineWidth : 0
    border.color: enabled ? Style.outlineVariant : Style.onSurface_12
    Accessible.checkable: checkable
    Accessible.checked: checked
}
