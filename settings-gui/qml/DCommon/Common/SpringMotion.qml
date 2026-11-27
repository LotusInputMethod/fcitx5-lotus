// Qt 6.2+ compatible SpringMotion (uses Timer instead of Qt 6.4+ FrameAnimation)
import QtQuick

QtObject {
    id: root

    property bool enabled: true
    property bool reducedMotion: false
    readonly property bool systemReducedMotion: Style.reduceMotion
    readonly property bool effectiveReducedMotion: reducedMotion || systemReducedMotion

    property real stiffness: 100
    property real damping: 16
    property real mass: 1
    property real positionEpsilon: 0.01
    property real velocityEpsilon: 0.01

    property real value: 0
    property real target: value
    property real velocity: 0

    property bool _running: false
    readonly property bool running: _running
    readonly property bool settled: !_running
    readonly property real timeConstantMs: effectiveReducedMotion ? 0 : 2000 * mass / Math.max(1, damping)
    property int settleDurationMs: 0
    property double _lastTickMs: 0

    function isSettled() {
        return Math.abs(target - value) <= positionEpsilon && Math.abs(velocity) <= velocityEpsilon;
    }

    function snapTo(v) {
        target = v;
        value = v;
        velocity = 0;
        _running = false;
    }

    function retarget(v) {
        if (!enabled || effectiveReducedMotion) {
            snapTo(v);
            return;
        }
        if (target === v && !_running)
            return;
        target = v;
        const tau = timeConstantMs;
        const distance = Math.abs(target - value);
        settleDurationMs = tau > 0 ? Math.round(Math.max(tau * 3, tau * Math.log(Math.max(distance, positionEpsilon) / positionEpsilon))) : 0;
        if (!isSettled()) {
            _lastTickMs = Date.now();
            _running = true;
        }
    }

    function advance(rawFrameTime) {
        if (!enabled || !_running || effectiveReducedMotion)
            return;

        if (rawFrameTime <= 0)
            return;
        const clampedDt = Math.min(0.05, rawFrameTime);
        const matrix = SpringSolver.frameCoefficients(stiffness, damping, mass, clampedDt);
        const displacement = value - target;
        const nextValue = target + matrix[0] * displacement + matrix[1] * velocity;
        const nextVelocity = matrix[2] * displacement + matrix[3] * velocity;

        if (Math.abs(target - nextValue) <= positionEpsilon && Math.abs(nextVelocity) <= velocityEpsilon) {
            velocity = 0;
            value = target;
            _running = false;
            return;
        }

        velocity = nextVelocity;
        value = nextValue;
    }

    onEffectiveReducedMotionChanged: {
        if (effectiveReducedMotion)
            snapTo(target);
    }

    onEnabledChanged: {
        if (!enabled) {
            snapTo(target);
            return;
        }
        retarget(target);
    }

    property Timer driver: Timer {
        interval: 16
        repeat: true
        running: root.enabled && root._running && !root.effectiveReducedMotion
        onTriggered: {
            const now = Date.now();
            const dt = root._lastTickMs > 0 ? (now - root._lastTickMs) / 1000.0 : 0.016;
            root._lastTickMs = now;
            root.advance(dt > 0 ? dt : 0.016);
        }
    }
}
