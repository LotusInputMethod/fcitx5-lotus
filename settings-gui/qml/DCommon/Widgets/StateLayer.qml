import QtQuick
import QtQuick.Templates as T
import DCommon.Common

MouseArea {
    id: root

    property bool disabled: false
    property bool hovered: control ? control.hovered : containsMouse
    property bool focused: control ? control.visualFocus : false
    property T.AbstractButton control: null
    property color stateColor: Style.surfaceText
    property real cornerRadius: parent && parent.radius !== undefined ? parent.radius : Style.cornerRadius
    property var tooltipText: null
    property bool tooltipMultiline: false
    property string tooltipSide: "bottom"
    property bool enableRipple: Style.enableRippleEffects
    property int transitionDuration: Style.expressiveDurations.expressiveFastEffects
    property var transitionCurve: Style.expressiveCurves.expressiveFastEffects

    readonly property bool isPressed: control ? control.down : pressed
    readonly property bool rippleDrawsPress: enableRipple && !Style.reduceMotion
    readonly property real stateOpacity: {
        if (disabled)
            return 0;
        if (isPressed && !rippleDrawsPress)
            return Style.stateLayerPressed;
        if (focused)
            return Style.stateLayerFocus;
        if (hovered)
            return Style.stateLayerHover;
        return 0;
    }
    readonly property bool controlPressed: control ? control.pressed : false

    property Rectangle _stateRect: null
    property DRipple _ripple: null
    property bool _completed: false

    anchors.fill: parent
    cursorShape: disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
    hoverEnabled: true
    acceptedButtons: control ? Qt.NoButton : Qt.LeftButton

    readonly property bool controlFocused: control ? control.visualFocus : false

    function showTooltip() {
        presentTooltip(0);
    }

    function presentTooltip(delay) {
        if (!tooltipText)
            return;
        tooltipLoader.active = true;
        const host = tooltipLoader.item;
        host?.present(delay);
        if (!host?.shown)
            tooltipLoader.active = false;
    }

    function pressRipple(x, y) {
        if (disabled || !rippleDrawsPress)
            return;
        if (!_ripple)
            _ripple = rippleComponent.createObject(root);
        _ripple.trigger(x, y, true);
    }

    onPressed: mouse => pressRipple(mouse.x, mouse.y)
    onReleased: _ripple?.release()
    onCanceled: _ripple?.release()

    onControlPressedChanged: {
        if (!controlPressed) {
            _ripple?.release();
            return;
        }
        const point = mapFromItem(control, control.pressX, control.pressY);
        pressRipple(point.x, point.y);
    }

    onStateOpacityChanged: {
        if (stateOpacity > 0 && !_stateRect)
            _stateRect = stateRectComponent.createObject(root);
    }

    Component.onCompleted: _completed = true

    Component {
        id: stateRectComponent

        Rectangle {
            property real stateAlpha: root._stateRect ? root.stateOpacity : 0

            anchors.fill: parent
            z: -1
            radius: root.cornerRadius
            color: Style.withAlpha(root.stateColor, stateAlpha)

            Behavior on stateAlpha {
                enabled: root._completed && !Style.reduceMotion
                DAnim {
                    duration: root.transitionDuration
                    easing.bezierCurve: root.transitionCurve
                }
            }
        }
    }

    Component {
        id: rippleComponent

        DRipple {
            anchors.fill: parent
            z: -1
            rippleColor: root.stateColor
            cornerRadius: root.cornerRadius
            enableRipple: root.enableRipple
        }
    }

    onEntered: presentTooltip(Style.tooltipDelay)

    onExited: {
        if (!controlFocused)
            tooltipLoader.item?.dismiss();
    }

    onControlFocusedChanged: {
        if (controlFocused) {
            presentTooltip(0);
            return;
        }
        if (!containsMouse)
            tooltipLoader.item?.dismiss();
    }

    Loader {
        id: tooltipLoader
        active: false
        sourceComponent: DTooltipHost {
            text: root.tooltipText
            multiline: root.tooltipMultiline
            target: root
            side: root.tooltipSide
            enabled: !root.disabled

            readonly property bool idle: !shown && !presenting
            onIdleChanged: {
                if (!idle) {
                    releaseTimer.stop();
                    return;
                }
                releaseTimer.restart();
            }

            Timer {
                id: releaseTimer
                interval: Style.shorterDuration
                onTriggered: tooltipLoader.active = false
            }
        }
    }
}
