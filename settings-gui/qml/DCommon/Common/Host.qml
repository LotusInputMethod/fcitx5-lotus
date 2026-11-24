pragma Singleton

import QtQuick

QtObject {
    property var session: null
    property var cache: null
    property var files: null
    property var appearance: null
    property bool hyprlandFocusGrab: false
    property bool ownWindowDecorations: false
}
