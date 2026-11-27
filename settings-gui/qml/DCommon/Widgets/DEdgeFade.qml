import QtQuick
import QtQuick.Window
import DCommon.Common
import "../Common/Surface.js" as Surface

Item {
    id: root

    required property Flickable target
    property real length: Style.spacingXL

    property var backdropHosts: []
    readonly property var hostWindow: root.Window.window
    readonly property color color: {
        const paints = [];
        for (const host of backdropHosts)
            addPaint(paints, paintOf(host), !paintsOwnColor(host));
        if (hostWindow)
            addPaint(paints, hostWindow.surfaceColor ?? hostWindow.backgroundColor ?? hostWindow.color, true);
        const flat = Surface.composite(paints);
        if (flat.a > 0)
            return Qt.rgba(flat.r, flat.g, flat.b, flat.a);
        return Surface.isFloatingWindow(target) ? Style.floatingWindowSurface : Style.hostSurface;
    }
    readonly property color clear: Style.withAlpha(color, 0)
    readonly property real topStrength: Math.min(1, Math.max(0, (target.contentY - target.originY) / length))
    readonly property real bottomStrength: Math.min(1, Math.max(0, (target.originY + target.contentHeight - target.height - target.contentY) / length))

    function addPaint(paints, paint, declared) {
        if (!paint || paint.a <= 0)
            return;
        const below = paints[paints.length - 1];
        if (declared && below && Qt.colorEqual(paint, below))
            return;
        paints.push(paint);
    }

    function paintsOwnColor(host) {
        return host instanceof Rectangle && host.color.a > 0;
    }

    function paintOf(host) {
        if (!host?.visible)
            return null;
        if (paintsOwnColor(host))
            return Style.withAlpha(host.color, host.color.a * host.opacity);
        return host.surfaceColor ?? host.backgroundColor ?? null;
    }

    function declaresSurface(item) {
        return item.surfaceColor !== undefined || item.backgroundColor !== undefined;
    }

    function covers(item) {
        const area = item.mapFromItem(root, 0, 0, root.width, root.height);
        return area.x >= -1 && area.y >= -1 && area.x + area.width <= item.width + 1 && area.y + area.height <= item.height + 1;
    }

    function rectanglesBehind(node) {
        const siblings = node.parent.children;
        const behind = [];
        let nodePassed = false;
        for (let i = 0; i < siblings.length; i++) {
            const sibling = siblings[i];
            if (sibling === node) {
                nodePassed = true;
                continue;
            }
            if (!(sibling instanceof Rectangle) || sibling.gradient)
                continue;
            if (sibling.z > node.z || (sibling.z === node.z && nodePassed))
                continue;
            if (!covers(sibling))
                continue;
            behind.push({
                item: sibling,
                order: i
            });
        }
        behind.sort((a, b) => b.item.z - a.item.z || b.order - a.order);
        return behind.map(entry => entry.item);
    }

    function resolveBackdropHosts() {
        const hosts = [];
        for (let node = target; node.parent; node = node.parent) {
            const parent = node.parent;
            hosts.push(...rectanglesBehind(node));
            if (parent instanceof Rectangle ? !parent.gradient : declaresSurface(parent))
                hosts.push(parent);
        }
        return hosts;
    }

    function refresh() {
        backdropHosts = resolveBackdropHosts();
    }

    Component.onCompleted: {
        refresh();
        settle.restart();
    }
    onWidthChanged: settle.restart()
    onHeightChanged: settle.restart()
    onVisibleChanged: settle.restart()

    Timer {
        id: settle
        interval: 0
        onTriggered: root.refresh()
    }

    Connections {
        target: root.target

        function onParentChanged() {
            settle.restart();
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.length
        opacity: root.topStrength
        visible: opacity > 0
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.color
            }
            GradientStop {
                position: 1
                color: root.clear
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.length
        opacity: root.bottomStrength
        visible: opacity > 0
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.clear
            }
            GradientStop {
                position: 1
                color: root.color
            }
        }
    }
}
