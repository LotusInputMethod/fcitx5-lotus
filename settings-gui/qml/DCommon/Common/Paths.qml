pragma Singleton

import QtQuick

QtObject {
    id: root

    property var backend: null

    readonly property bool iconThemeLive: false
    readonly property int iconThemeRevision: 0
    readonly property bool ready: backend?.ready ?? true

    function stringify(path) {
        if (backend)
            return backend.stringify(path);
        const raw = path.toString();
        try {
            return decodeURIComponent(raw);
        } catch (e) {
            return raw;
        }
    }

    function strip(path) {
        if (backend)
            return backend.strip(path);
        return stringify(path).replace("file://", "");
    }

    function resolveIconPath(iconName) {
        if (backend && typeof backend.resolveIconPath === "function")
            return backend.resolveIconPath(iconName);
        return themeIconUrl(iconName);
    }

    function themeIconUrl(iconName, fallbackName) {
        if (!iconName)
            return "";
        if (iconName.startsWith("image://") || iconName.startsWith("file://") || iconName.startsWith("qrc:/"))
            return iconName;
        return "image://theme/" + iconName;
    }
}
