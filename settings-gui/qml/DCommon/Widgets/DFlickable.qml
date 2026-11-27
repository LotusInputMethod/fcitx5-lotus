// Qt 6.2+ compatible Flickable with smooth wheel/touchpad scrolling and edge fades
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import DCommon.Common
import "ScrollConstants.js" as Scroll
import "Reveal.js" as Reveal
import "../Common/WheelInput.js" as WheelInput

Flickable {
    id: flickable

    property alias verticalScrollBar: vbar
    property alias horizontalScrollBar: hbar
    property bool showScrollBar: true
    property bool horizontal: false
    property bool fadeEdges: true
    property real fadeLength: Style.spacingXL
    property real fadeTopInset: 0
    property real fadeBottomInset: 0
    property real fadeSideInset: 0
    readonly property real revealInsetTop: fadeEdges && Style.scrollShadowsEnabled ? fadeTopInset + fadeLength : 0
    readonly property real revealInsetBottom: fadeEdges && Style.scrollShadowsEnabled ? fadeBottomInset + fadeLength : 0

    function revealRange(top, bottom) {
        contentY = Reveal.contentYFor(flickable, top, bottom, revealInsetTop, revealInsetBottom);
    }
    property real mouseWheelSpeed: Scroll.mouseWheelSpeed
    property bool wheelEnabled: true
    property real momentumVelocity: 0
    property bool isMomentumActive: false
    readonly property bool windowVisible: flickable.Window.window?.visible ?? false
    property real friction: Scroll.friction
    property bool _scrollBarActive: false
    property double _lastMomentumMs: 0
    readonly property var _bar: horizontal ? hbar : vbar

    function _axisPos() {
        return horizontal ? contentX : contentY;
    }

    function _axisMax() {
        return Math.max(0, horizontal ? contentWidth - width : contentHeight - height);
    }

    function _scrollTo(pos) {
        const clamped = Math.max(0, Math.min(_axisMax(), pos));
        if (horizontal)
            contentX = clamped;
        else
            contentY = clamped;
    }

    flickDeceleration: Scroll.flickDeceleration
    maximumFlickVelocity: Scroll.maximumFlickVelocity
    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.FollowBoundsBehavior
    pressDelay: 0
    flickableDirection: horizontal ? Flickable.HorizontalFlick : Flickable.VerticalFlick

    WheelHandler {
        id: wheelHandler
        enabled: flickable.wheelEnabled

        property real touchpadSpeed: Scroll.touchpadSpeed
        property real momentumRetention: Scroll.momentumRetention
        property real lastWheelTime: 0
        property real momentum: 0
        property var velocitySamples: []
        property bool sessionUsedMouseWheel: false

        function startMomentum() {
            if (!flickable.visible || !flickable.Window.window?.visible)
                return;
            flickable._lastMomentumMs = Date.now();
            flickable.isMomentumActive = true;
            momentumAnim.running = true;
        }

        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

        onWheel: event => handleWheel(event)

        function handleWheel(event) {
            if (flickable.horizontal && !WheelInput.isHorizontal(event)) {
                event.accepted = false;
                return;
            }
            flickable._bar._scrollBarActive = true;
            flickable._bar.hideTimer.restart();

            const currentTime = Date.now();
            const timeDelta = currentTime - lastWheelTime;
            lastWheelTime = currentTime;

            const deltaY = flickable.horizontal ? event.angleDelta.x || event.angleDelta.y : event.angleDelta.y;
            const pixelDelta = flickable.horizontal ? event.pixelDelta.x || event.pixelDelta.y : event.pixelDelta.y;
            const kind = WheelInput.axisKind(event, deltaY);

            if (kind === WheelInput.Kind.wheel) {
                sessionUsedMouseWheel = true;
                momentumAnim.running = false;
                flickable.isMomentumActive = false;
                velocitySamples = [];
                momentum = 0;
                flickable.momentumVelocity = 0;

                const lines = Math.round(Math.abs(deltaY) / 120);
                const scrollAmount = (deltaY > 0 ? -lines : lines) * flickable.mouseWheelSpeed;

                if (flickable.flicking) {
                    flickable.cancelFlick();
                }

                flickable._scrollTo(flickable._axisPos() + scrollAmount);
            } else if (kind === WheelInput.Kind.highResWheel) {
                sessionUsedMouseWheel = true;
                momentumAnim.running = false;
                flickable.isMomentumActive = false;
                velocitySamples = [];
                momentum = 0;
                flickable.momentumVelocity = 0;

                const delta = deltaY / 8 * touchpadSpeed;

                if (flickable.flicking) {
                    flickable.cancelFlick();
                }

                flickable._scrollTo(flickable._axisPos() - delta);
            } else if (kind === WheelInput.Kind.touchpad) {
                sessionUsedMouseWheel = false;
                momentumAnim.running = false;
                flickable.isMomentumActive = false;

                let delta = pixelDelta * touchpadSpeed;

                velocitySamples.push({
                    "delta": delta,
                    "time": currentTime
                });
                velocitySamples = velocitySamples.filter(s => currentTime - s.time < Scroll.velocitySampleWindowMs);

                if (velocitySamples.length > 1) {
                    const totalDelta = velocitySamples.reduce((sum, s) => sum + s.delta, 0);
                    const timeSpan = currentTime - velocitySamples[0].time;
                    if (timeSpan > 0) {
                        flickable.momentumVelocity = Math.max(-Scroll.maxMomentumVelocity, Math.min(Scroll.maxMomentumVelocity, totalDelta / timeSpan * 1000));
                    }
                }

                if (timeDelta < Scroll.momentumTimeThreshold) {
                    momentum = momentum * momentumRetention + delta * Scroll.momentumDeltaFactor;
                    delta += momentum;
                } else {
                    momentum = 0;
                }

                if (flickable.flicking) {
                    flickable.cancelFlick();
                }

                flickable._scrollTo(flickable._axisPos() - delta);
            }

            event.accepted = true;
        }

        onActiveChanged: {
            if (!active)
                release();
        }

        function release() {
            if (!sessionUsedMouseWheel && Math.abs(flickable.momentumVelocity) >= Scroll.minMomentumVelocity) {
                startMomentum();
            } else {
                velocitySamples = [];
                flickable.momentumVelocity = 0;
            }
        }
    }

    WheelHandler {
        enabled: flickable.wheelEnabled && flickable.horizontal && !flickable.interactive
        orientation: Qt.Horizontal
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            if (event.angleDelta.y === 0 && event.pixelDelta.y === 0)
                wheelHandler.handleWheel(event);
        }
    }

    onMovementStarted: {
        _bar._scrollBarActive = true;
        _bar.hideTimer.stop();
    }
    onMovementEnded: _bar.hideTimer.restart()

    function forwardWheel(event) {
        if (!wheelEnabled)
            return;
        wheelHandler.handleWheel(event);
    }

    function forwardWheelEnd() {
        wheelHandler.release();
    }

    function stopMomentum() {
        cancelFlick();
        momentumAnim.running = false;
        isMomentumActive = false;
        momentumVelocity = 0;
        wheelHandler.momentum = 0;
        wheelHandler.velocitySamples = [];
    }

    onVisibleChanged: {
        if (!visible)
            stopMomentum();
    }

    onWindowVisibleChanged: {
        if (!windowVisible)
            stopMomentum();
    }

    Timer {
        id: momentumAnim
        interval: 16
        repeat: true
        running: false

        onTriggered: {
            const now = Date.now();
            const dt = flickable._lastMomentumMs > 0 ? Math.min(0.05, (now - flickable._lastMomentumMs) / 1000.0) : 0.016;
            flickable._lastMomentumMs = now;

            const next = flickable._axisPos() - flickable.momentumVelocity * dt;
            const max = flickable._axisMax();

            if (next < 0 || next > max) {
                flickable._scrollTo(next);
                running = false;
                flickable.isMomentumActive = false;
                flickable.momentumVelocity = 0;
                return;
            }

            flickable._scrollTo(next);
            flickable.momentumVelocity *= Math.pow(flickable.friction, dt / 0.016);

            if (Math.abs(flickable.momentumVelocity) < Scroll.momentumStopThreshold) {
                running = false;
                flickable.isMomentumActive = false;
                flickable.momentumVelocity = 0;
            }
        }
    }

    Loader {
        parent: flickable
        anchors.fill: parent
        anchors.topMargin: flickable.fadeTopInset
        anchors.bottomMargin: flickable.fadeBottomInset
        anchors.leftMargin: flickable.fadeSideInset
        anchors.rightMargin: flickable.fadeSideInset
        z: 1
        active: Style.scrollShadowsEnabled && flickable.fadeEdges && !flickable.horizontal && flickable.contentHeight > flickable.height + 1
        sourceComponent: DEdgeFade {
            target: flickable
            length: flickable.fadeLength
        }
    }

    ScrollBar.vertical: DScrollbar {
        id: vbar
        z: 2
        targetFlickable: flickable
        allowed: flickable.showScrollBar && !flickable.horizontal
    }

    ScrollBar.horizontal: DHorizontalScrollbar {
        id: hbar
        targetFlickable: flickable
        allowed: flickable.showScrollBar && flickable.horizontal
    }
}
