import QtQuick
import QtQuick.Controls
import DCommon.Common

ScrollBar {
    id: scrollbar

    property Flickable targetFlickable: null
    property bool allowed: true
    property bool _scrollBarActive: false

    readonly property bool scrollable: !!targetFlickable && targetFlickable.contentWidth > targetFlickable.width
    readonly property bool targetMoving: !!targetFlickable && (targetFlickable.moving || targetFlickable.flicking || targetFlickable.isMomentumActive === true)
    readonly property bool shouldShow: pressed || hovered || active || targetMoving || hideTimer.running || _scrollBarActive
    property alias hideTimer: hideScrollBarTimer

    orientation: Qt.Horizontal
    policy: Style.scrollbarsEnabled && allowed && scrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    minimumSize: 0.08
    padding: 0
    topPadding: Style.scrollbarGap
    bottomPadding: Style.scrollbarGap
    leftPadding: Style.spacingXXS
    rightPadding: Style.spacingXXS
    z: 2
    interactive: true
    hoverEnabled: true
    enabled: targetFlickable?.enabled ?? false
    visible: policy !== ScrollBar.AlwaysOff && (targetFlickable?.visible ?? false)
    opacity: policy !== ScrollBar.AlwaysOff && shouldShow ? 1 : 0

    onTargetMovingChanged: {
        if (!targetMoving)
            hideTimer.restart();
    }

    Behavior on opacity {
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    contentItem: Rectangle {
        implicitHeight: Style.scrollbarThickness
        radius: Style.fullRadius(width, height)
        color: scrollbar.pressed ? Style.primary : Style.outline
    }

    background: Item {}

    Timer {
        id: hideScrollBarTimer
        interval: Style.scrollbarHideDelay
    }
}
