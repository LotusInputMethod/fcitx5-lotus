import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    width: 800
    height: 300
    color: "#1e1e1e"
    
    Text {
        anchors.centerIn: parent
        text: "Lotus OSK (C++)"
        color: "white"
        font.pixelSize: 24
    }
    
    Grid {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        columns: 10
        spacing: 5
        padding: 10
        
        Repeater {
            model: ["Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"]
            Button {
                text: modelData
                width: 70
                height: 50
                onClicked: console.log("Pressed: " + text)
            }
        }
    }
}
