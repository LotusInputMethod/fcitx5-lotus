import QtQuick
import DCommon.Common

Rectangle {
    id: root

    property bool shown: true
    property bool blinkOn: true
    property int blinkTimeout: 10000

    readonly property int flashTime: Qt.styleHints.cursorFlashTime
    readonly property bool blinkEnabled: shown && flashTime > 1

    function resetBlink() {
        blinkOn = true;
        if (!blinkEnabled)
            return;
        blinkTimer.restart();
        if (blinkTimeout <= 0)
            return;
        blinkTimeoutTimer.restart();
    }

    width: 2
    radius: 1
    color: Style.primary
    visible: shown && blinkOn

    onShownChanged: resetBlink()

    Timer {
        id: blinkTimer
        running: root.blinkEnabled
        interval: root.flashTime / 2
        repeat: true
        onTriggered: root.blinkOn = !root.blinkOn
    }

    Timer {
        id: blinkTimeoutTimer
        running: blinkTimer.running && root.blinkTimeout > 0
        interval: root.blinkTimeout
        onTriggered: {
            blinkTimer.stop();
            root.blinkOn = true;
        }
    }
}
