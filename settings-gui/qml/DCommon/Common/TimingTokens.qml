import QtQuick

QtObject {
    readonly property int frame: 16
    readonly property int cappedTick: 16
    readonly property int slowTick: 50
    readonly property int second: 1000
    readonly property int minute: 60000

    readonly property int debounceFast: 50
    readonly property int debounceShort: 100
    readonly property int debounce: 150
    readonly property int debounceLong: 250
    readonly property int debounceSlow: 400

    readonly property int settleShort: 100
    readonly property int settle: 250
    readonly property int settleLong: 500
    readonly property int settleSlow: 1000

    readonly property int holdShort: 1500
    readonly property int hold: 2000
    readonly property int holdMedium: 3000
    readonly property int holdLong: 5000
    readonly property int holdExtraLong: 8000

    readonly property int timeoutShort: 5000
    readonly property int timeout: 10000
    readonly property int timeoutLong: 30000

    readonly property int pollFast: 5000
    readonly property int poll: 10000
    readonly property int pollSlow: 30000
}
