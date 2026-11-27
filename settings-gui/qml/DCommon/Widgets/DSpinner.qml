// Qt 6.2+ compatible Material spinner using Canvas (no QtQuick.Shapes dependency)
import QtQuick
import DCommon.Common

Item {
    id: root

    property real size: 48
    property real strokeWidth: Math.min(4, Math.max(2, size / 10))
    property color color: Style.primary
    property bool running: visible

    implicitWidth: size
    implicitHeight: size

    onColorChanged: arcCanvas.requestPaint()
    onStrokeWidthChanged: arcCanvas.requestPaint()
    onWidthChanged: arcCanvas.requestPaint()
    onHeightChanged: arcCanvas.requestPaint()

    Item {
        id: rotator
        anchors.fill: parent
        property real globalRotation: 0
        rotation: globalRotation

        NumberAnimation on globalRotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: root.running
        }

        Canvas {
            id: arcCanvas
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const r = Math.max(1, (Math.min(width, height) - root.strokeWidth) / 2);
                ctx.strokeStyle = root.color;
                ctx.lineWidth = root.strokeWidth;
                ctx.lineCap = "round";
                ctx.beginPath();
                ctx.arc(width / 2, height / 2, r, -Math.PI / 2, Math.PI * 0.9, false);
                ctx.stroke();
            }
        }
    }
}
