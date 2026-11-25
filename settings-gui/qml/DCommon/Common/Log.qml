pragma Singleton

import QtQuick

QtObject {
    property var backend: null

    readonly property var _noop: ({
            debug: function () {},
            info: function () {},
            warn: function () {},
            error: function () {},
            fatal: function () {}
        })

    function scoped(module) {
        return backend ? backend.scoped(module) : _noop;
    }
}
