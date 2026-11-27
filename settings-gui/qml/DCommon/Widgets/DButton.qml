import QtQuick
import DCommon.Common

StyledButton {
    id: root

    property bool busy: false
    property string reserveText: ""
    property string shape: "round"
    property real maximumWidth: Infinity
    property bool wrapText: false
    property string iconName: ""
    property int iconSize: root.buttonHeight <= Style.buttonHeightS ? Style.iconSizeMedium : Style.iconSize
    property string variant: "filled"
    property bool danger: false
    readonly property bool hasContainer: variant === "filled" || variant === "tonal"
    property color backgroundColor: {
        switch (variant) {
        case "tonal":
            return danger ? Style.errorContainer : Style.secondaryContainer;
        case "outlined":
        case "text":
            return "transparent";
        }
        return danger ? Style.error : Style.buttonBg;
    }
    property color textColor: {
        switch (variant) {
        case "tonal":
            return danger ? Style.onErrorContainer : Style.onSecondaryContainer;
        case "outlined":
        case "text":
            return danger ? Style.error : Style.primary;
        }
        return danger ? Style.onError : Style.buttonText;
    }
    property color focusRingColor: Style.focusRingColor
    property int buttonHeight: Style.buttonHeightS
    readonly property real labelSize: buttonHeight >= Style.buttonHeightM ? Style.fontSizeLarge : Style.fontSizeMedium
    horizontalPadding: {
        if (buttonHeight <= Style.buttonHeightXS)
            return Style.spacingM;
        if (buttonHeight <= Style.buttonHeightS)
            return Style.spacingL;
        return Style.spacingXL;
    }
    property bool enableScaleAnimation: false
    property bool enableRipple: Style.enableRippleEffects
    property real minimumWidth: Style.buttonMinWidth
    property var tooltipText: null
    property string tooltipSide: "bottom"

    implicitWidth: Math.min(maximumWidth, Math.max(contentRow.implicitWidth + horizontalPadding * 2, minimumWidth))
    implicitHeight: wrapText ? Math.max(buttonHeight, contentRow.implicitHeight + Style.spacingS * 2) : buttonHeight
    readonly property color contentColor: enabled ? textColor : Style.onSurface_38
    readonly property real maximumLabelWidth: Math.max(0, maximumWidth - horizontalPadding * 2 - (busy || iconName ? iconSize + contentRow.spacing : 0))

    readonly property real restRadius: Style.buttonRadius(width, height, buttonHeight, false, shape === "round")
    readonly property real pressedRadius: Style.buttonRadius(width, height, buttonHeight, true, shape === "round")
    property real pressProgress: pressed ? 1 : 0
    radius: restRadius + (pressedRadius - restRadius) * pressProgress
    color: enabled || !hasContainer ? backgroundColor : Style.onSurface_10
    border.width: variant === "outlined" ? Style.outlineWidth : 0
    border.color: enabled ? Style.outlineVariant : Style.onSurface_12
    scale: (enableScaleAnimation && pressed) ? Style.pressScale : 1.0
    Accessible.role: Accessible.Button
    Accessible.name: text || tooltipText || iconName

    FocusRing {
        visible: root.visualFocus
        border.color: root.focusRingColor
        radius: Math.max(0, parent.radius + Style.focusRingOffset)
    }

    Behavior on pressProgress {
        enabled: !Style.reduceMotion
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    Behavior on scale {
        enabled: enableScaleAnimation && !Style.reduceMotion
        DAnim {
            duration: Style.expressiveDurations.expressiveFastSpatial
            easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
        }
    }

    StateLayer {
        id: stateLayer
        control: root
        enabled: root.enabled
        disabled: !root.enabled
        stateColor: root.textColor
        enableRipple: root.enableRipple
        transitionDuration: Style.expressiveDurations.expressiveEffects
        transitionCurve: Style.expressiveCurves.expressiveEffects
        tooltipText: root.tooltipText
        tooltipSide: root.tooltipSide
    }

    Loader {
        id: reservedLabel
        active: root.reserveText !== ""
        visible: false
        sourceComponent: StyledText {
            text: root.reserveText
            font.pixelSize: root.labelSize
            font.weight: Style.fontWeightMedium
            wrapMode: Text.NoWrap
        }
    }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: {
            if (buttonHeight <= Style.buttonHeightXS) {
                return Style.spacingXS;
            } else if (buttonHeight <= Style.buttonHeightM) {
                return Style.spacingS;
            } else {
                return Style.spacingM;
            }
        }

        Item {
            width: root.iconSize
            height: root.iconSize
            visible: root.busy || root.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter

            DIcon {
                name: root.iconName
                size: root.iconSize
                color: root.contentColor
                visible: !root.busy
            }
            Loader {
                anchors.fill: parent
                active: root.busy
                sourceComponent: DSpinner {
                    size: root.iconSize
                    strokeWidth: Style.outlineWidthFocused
                    color: root.contentColor
                    running: root.busy && root.visible
                    Accessible.ignored: true
                }
            }
        }

        StyledText {
            width: Math.min(Math.max(implicitWidth, reservedLabel.item?.implicitWidth ?? 0), root.maximumLabelWidth)
            wrapMode: root.wrapText ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
            elide: root.wrapText ? Text.ElideNone : Text.ElideRight
            text: root.text
            font.pixelSize: root.labelSize
            font.weight: Style.fontWeightMedium
            color: root.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
