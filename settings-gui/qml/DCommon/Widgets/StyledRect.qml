import QtQuick
import DCommon.Common

Rectangle {
    color: "transparent"
    radius: Style.cornerRadiusM

    Behavior on opacity {
        NumberAnimation {
            duration: Style.expressiveDurations.expressiveSlowEffects
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.expressiveSlowEffects
        }
    }
}
