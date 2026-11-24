.pragma library

// Qt Wayland sends scroll phases only for the finger axis source; pixelDelta also comes with wheels from Qt 6.12 (qtbase d19bd66b4)
const Kind = Object.freeze({
    none: "none",
    touchpad: "touchpad",
    wheel: "wheel",
    highResWheel: "highResWheel"
});

function isTouchpad(wheel) {
    if (wheel.phase === Qt.NoScrollPhase)
        return false;
    return wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0;
}

function verticalKind(wheel) {
    return axisKind(wheel, wheel.angleDelta.y);
}

function axisKind(wheel, angle) {
    if (isTouchpad(wheel))
        return Kind.touchpad;
    if (angle === 0)
        return Kind.none;
    return angle % 120 === 0 ? Kind.wheel : Kind.highResWheel;
}

function anyAxisDelta(wheel) {
    const point = isTouchpad(wheel) ? wheel.pixelDelta : wheel.angleDelta;
    return point.x || point.y;
}

function isHorizontal(wheel) {
    if (wheel.modifiers & Qt.ShiftModifier)
        return true;
    return Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y);
}

function sidewaysDelta(wheel, notch) {
    if (isTouchpad(wheel))
        return dominantDelta(wheel.pixelDelta);
    return dominantDelta(wheel.angleDelta) / 120 * notch;
}

function dominantDelta(point) {
    return Math.abs(point.x) > Math.abs(point.y) ? point.x : point.y;
}
