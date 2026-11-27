import QtQuick
import DCommon.Common
import "../Common/Contrast.js" as Contrast

StyledButton {
    id: root

    property string iconName: ""
    property int iconSize: Style.iconSizeMedium
    property color iconColor: Style.onSurfaceVariant
    property bool iconFilled: false
    property color backgroundColor: "transparent"
    property color stateColor: iconColor
    property real stateLayerInset: 0
    property color focusRingColor: backgroundColor.a === 0 || Contrast.ratio(Style.focusRingColor, backgroundColor) >= 3 ? Style.focusRingColor : iconColor
    property bool circular: true
    property int buttonSize: Style.buttonHeightXS
    property var tooltipText: null
    property string tooltipSide: "bottom"
    property int shapeDuration: Style.expressiveDurations.expressiveEffects
    property var shapeCurve: Style.expressiveCurves.expressiveEffects
    property int stateDuration: Style.expressiveDurations.expressiveFastEffects
    property var stateCurve: Style.expressiveCurves.expressiveFastEffects

    signal entered
    signal exited

    function showTooltip() {
        stateLayer.showTooltip();
    }

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    radius: Style.buttonRadius(width, height, buttonSize, pressed, circular)
    color: enabled || backgroundColor.a === 0 ? backgroundColor : Style.onSurface_10
    Accessible.role: Accessible.Button
    Accessible.name: tooltipText || iconName

    Behavior on radius {
        enabled: root.visible && !Style.reduceMotion
        DAnim {
            duration: root.shapeDuration
            easing.bezierCurve: root.shapeCurve
        }
    }

    FocusRing {
        visible: root.visualFocus
        border.color: root.focusRingColor
        anchors.margins: root.stateLayerInset + Style.focusRingWidth / 2
        radius: Math.max(0, parent.radius - root.stateLayerInset - Style.focusRingWidth / 2)
    }

    DIcon {
        anchors.centerIn: parent
        name: root.iconName
        size: root.iconSize
        filled: root.iconFilled
        color: root.enabled ? root.iconColor : Style.onSurface_38
    }

    StateLayer {
        id: stateLayer
        anchors.margins: root.stateLayerInset
        cornerRadius: Math.max(0, root.radius - root.stateLayerInset)
        control: root
        disabled: !root.enabled
        stateColor: root.stateColor
        transitionDuration: root.stateDuration
        transitionCurve: root.stateCurve
        onEntered: root.entered()
        onExited: root.exited()
        tooltipText: root.tooltipText
        tooltipSide: root.tooltipSide
    }
}
