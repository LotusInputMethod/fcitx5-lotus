// Qt 6.2+ compatible clipped-Rectangle press ripple (no ShaderEffect/.qsb)
import QtQuick
import Quickshell.Widgets
import DCommon.Common

ClippingRectangle {
    id: root

    property color rippleColor: Style.primary
    property real cornerRadius: 0
    property bool enableRipple: Style.enableRippleEffects
    property int animationDuration: Style.motionMs(225)

    readonly property int fadeInDuration: Style.motionMs(75)
    readonly property int fadeOutDuration: Style.motionMs(150)
    readonly property var growCurve: [0.4, 0, 0.2, 1, 1, 1]
    readonly property real startRadiusRatio: 0.3
    readonly property real endRadiusPadding: 10

    property Item fxItem: null
    readonly property bool animating: fxItem?.animating ?? false

    anchors.fill: parent
    radius: cornerRadius
    color: "transparent"

    function trigger(x, y, hold) {
        if (!enableRipple || Style.reduceMotion)
            return;
        if (!fxItem)
            fxItem = fxComponent.createObject(root);
        fxItem.trigger(x, y, hold === true);
    }

    function release() {
        fxItem?.release();
    }

    Component {
        id: fxComponent

        Item {
            id: fx
            anchors.fill: parent

            property real pressX: 0
            property real pressY: 0
            property bool held: false
            readonly property real startRadius: Math.max(width, height) * root.startRadiusRatio
            readonly property real endRadius: Math.hypot(width, height) / 2 + root.endRadiusPadding
            readonly property bool animating: held || enter.running || fade.running

            function trigger(x, y, hold) {
                pressX = x;
                pressY = y;
                held = hold;
                fade.stop();
                enter.restart();
            }

            function release() {
                if (!held)
                    return;
                held = false;
                if (enter.running)
                    return;
                fade.restart();
            }

            ParallelAnimation {
                id: enter

                onFinished: {
                    if (!fx.held)
                        fade.restart();
                }

                NumberAnimation {
                    target: rippleCircle
                    property: "rippleOpacity"
                    to: Style.stateLayerPressed
                    duration: root.fadeInDuration
                }
                DAnim {
                    target: rippleCircle
                    property: "rippleRadius"
                    from: fx.startRadius
                    to: fx.endRadius
                    duration: root.animationDuration
                    easing.bezierCurve: root.growCurve
                }
                NumberAnimation {
                    target: rippleCircle
                    property: "rippleCenterX"
                    from: fx.pressX
                    to: fx.width / 2
                    duration: root.animationDuration
                }
                NumberAnimation {
                    target: rippleCircle
                    property: "rippleCenterY"
                    from: fx.pressY
                    to: fx.height / 2
                    duration: root.animationDuration
                }
            }

            NumberAnimation {
                id: fade
                target: rippleCircle
                property: "rippleOpacity"
                to: 0
                duration: root.fadeOutDuration
            }

            Rectangle {
                id: rippleCircle
                visible: fx.animating

                property real rippleCenterX: 0
                property real rippleCenterY: 0
                property real rippleRadius: 0
                property real rippleOpacity: 0

                x: rippleCenterX - rippleRadius
                y: rippleCenterY - rippleRadius
                width: rippleRadius * 2
                height: rippleRadius * 2
                radius: rippleRadius
                color: Style.withAlpha(root.rippleColor, rippleOpacity)
            }
        }
    }
}
