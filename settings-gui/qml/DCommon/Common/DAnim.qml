import QtQuick

NumberAnimation {
    duration: Style.expressiveDurations.expressiveDefaultSpatial
    easing.type: Easing.BezierSpline
    easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
}
