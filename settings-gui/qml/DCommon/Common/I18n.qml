pragma Singleton

import QtQuick

QtObject {
    id: root

    property var backend: null

    readonly property bool isRtl: backend && backend.isRtl !== undefined ? backend.isRtl : false

    function tr(term, context, isRealContext) {
        return backend && typeof backend.tr === "function" ? backend.tr(term) : term;
    }
}
