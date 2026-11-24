pragma Singleton

import QtQuick

QtObject {
    id: root

    readonly property Rounding rounding: Rounding {}
    readonly property Spacing spacing: Spacing {}
    readonly property FontSize fontSize: FontSize {}
    readonly property Anim anim: Anim {}

    component Rounding: QtObject {
        readonly property int small: 8
        readonly property int normal: 12
        readonly property int large: 16
        readonly property int extraLarge: 24
        readonly property int full: 1000
    }

    component Spacing: QtObject {
        readonly property int small: 4
        readonly property int normal: 8
        readonly property int large: 12
        readonly property int extraLarge: 16
        readonly property int huge: 24
    }

    component FontSize: QtObject {
        readonly property int small: 12
        readonly property int normal: 14
        readonly property int large: 16
        readonly property int extraLarge: 20
        readonly property int huge: 24
    }

    component AnimCurves: QtObject {
        readonly property var expressiveFastSpatial: [0.42, 1.67, 0.21, 0.9, 1, 1]
        readonly property var expressiveDefaultSpatial: [0.38, 1.21, 0.22, 1, 1, 1]
        readonly property var expressiveEffects: [0.34, 0.8, 0.34, 1, 1, 1]
    }

    component AnimDurations: QtObject {
        readonly property int quick: 150
        readonly property int normal: 300
        readonly property int slow: 500
        readonly property int extraSlow: 1000
        readonly property int expressiveFastSpatial: 350
        readonly property int expressiveDefaultSpatial: 500
        readonly property int expressiveEffects: 200
    }

    component Anim: QtObject {
        readonly property AnimCurves curves: AnimCurves {}
        readonly property AnimDurations durations: AnimDurations {}
    }
}
