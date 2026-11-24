import QtQuick
import DCommon.Common

Item {
    id: root

    property alias model: repeater.model
    property alias delegate: repeater.delegate
    readonly property int count: repeater.count
    property real spacing: Style.groupedListGap
    property var flickable: null
    property var group: null
    property string groupKey: ""
    property Item dropArea: root
    property bool externalDrag: group !== null
    property bool crossSectionActive: false
    property int gapIndex: -1
    property real gapHeight: Style.listItemTwoLineHeight
    property var order: []
    property int draggingIndex: -1
    property real pressOffset: 0
    property real pointerY: 0
    property int focusIndex: -1
    property var positions: []
    property int delegateRevision: 0
    property bool animateLayout: false
    property int focusReason: Qt.OtherFocusReason

    signal reordered(var indices)
    signal dragStarted(int index, point position)
    signal dragMoved(point position)
    signal dragFinished
    signal dragCanceled

    implicitHeight: 0
    height: implicitHeight
    width: parent?.width ?? 0

    function itemAt(index) {
        return repeater.itemAt(index);
    }

    function identityOrder() {
        return Array.from({
            length: count
        }, (_, i) => i);
    }

    function reset() {
        animateLayout = false;
        cancel();
        layout();
    }

    function layout() {
        let cursor = 0;
        const next = [];
        for (let slot = 0; slot < order.length; slot++) {
            if (slot === gapIndex)
                cursor += gapHeight + spacing;
            const index = order[slot];
            const item = itemAt(index);
            next[index] = cursor;
            if (!item)
                continue;
            item.width = width;
            if (index !== draggingIndex)
                item.y = cursor;
            cursor += item.height + spacing;
        }
        if (gapIndex === order.length)
            cursor += gapHeight + spacing;
        positions = next;
        implicitHeight = Math.max(0, cursor - spacing);
    }

    function insertionIndex(y) {
        for (let slot = 0; slot < order.length; slot++) {
            const index = order[slot];
            const item = itemAt(index);
            if (item && y < positions[index] + item.height / 2)
                return slot;
        }
        return count;
    }

    function begin(index, position) {
        const item = itemAt(index);
        if (!item || !enabled)
            return;
        animateLayout = true;
        focusReason = Qt.MouseFocusReason;
        draggingIndex = index;
        pressOffset = position.y - item.y;
        pointerY = position.y;
        dragStarted(index, position);
    }

    function dragTo(position) {
        const item = itemAt(draggingIndex);
        if (!item)
            return;
        pointerY = position.y;
        item.y = externalDrag ? position.y - pressOffset : Math.max(0, Math.min(height - item.height, position.y - pressOffset));
        dragMoved(position);
        if (crossSectionActive)
            return;
        const from = order.indexOf(draggingIndex);
        if (from < 0)
            return;
        let target = from;
        for (let slot = 0; slot < order.length; slot++) {
            const index = order[slot];
            if (slot === from)
                continue;
            const other = itemAt(index);
            if (!other)
                continue;
            const middle = positions[index] + other.height / 2;
            if (slot < from && item.y < middle)
                target = Math.min(target, slot);
            if (slot > from && item.y + item.height > middle)
                target = Math.max(target, slot);
        }
        if (target === from)
            return;
        const next = order.slice();
        next.splice(from, 1);
        next.splice(target, 0, draggingIndex);
        order = next;
    }

    function finish() {
        if (draggingIndex < 0)
            return;
        if (externalDrag) {
            dragFinished();
            return;
        }
        commit();
    }

    function commit() {
        if (draggingIndex < 0)
            return;
        const next = order.slice();
        focusIndex = next.indexOf(draggingIndex);
        draggingIndex = -1;
        crossSectionActive = false;
        gapIndex = -1;
        layout();
        if (next.some((index, slot) => index !== slot))
            reordered(next);
        animateLayout = false;
        order = identityOrder();
        focusTimer.restart();
    }

    function cancel() {
        const active = draggingIndex >= 0;
        draggingIndex = -1;
        crossSectionActive = false;
        gapIndex = -1;
        order = identityOrder();
        root.layout();
        if (active)
            dragCanceled();
    }

    function move(index, delta) {
        const target = index + delta;
        if (!enabled || draggingIndex >= 0 || index < 0 || index >= count || target < 0 || target >= count)
            return;
        const next = identityOrder();
        next.splice(index, 1);
        next.splice(target, 0, index);
        focusIndex = target;
        focusReason = Qt.TabFocusReason;
        reordered(next);
        order = identityOrder();
        focusTimer.restart();
    }

    onDragStarted: (index, position) => group?.begin(root, index, position)
    onDragMoved: position => group?.move(position)
    onDragFinished: group?.finish()
    onDragCanceled: group?.cancel()

    Component.onCompleted: group?.registerList(root)
    Component.onDestruction: group?.unregisterList(root)

    onModelChanged: reset()
    onCountChanged: reset()
    onOrderChanged: root.layout()
    onWidthChanged: root.layout()
    onSpacingChanged: root.layout()
    onGapIndexChanged: {
        if (gapIndex >= 0)
            animateLayout = true;
        root.layout();
    }
    onGapHeightChanged: root.layout()
    onCrossSectionActiveChanged: {
        if (crossSectionActive)
            order = identityOrder();
    }
    onVisibleChanged: {
        if (!visible)
            cancel();
    }
    onEnabledChanged: {
        if (!enabled) {
            cancel();
            return;
        }
        focusTimer.restart();
    }

    Repeater {
        id: repeater
        onItemAdded: {
            root.delegateRevision++;
            root.layout();
        }
        onItemRemoved: root.delegateRevision++
    }

    Instantiator {
        model: root.count
        QtObject {
            required property int index
            readonly property real itemHeight: {
                root.delegateRevision;
                return root.itemAt(index)?.height ?? 0;
            }
            onItemHeightChanged: root.layout()
        }
    }

    Timer {
        id: focusTimer
        interval: 0
        onTriggered: {
            if (!root.enabled)
                return;
            const item = root.itemAt(root.focusIndex);
            root.focusIndex = -1;
            if (!item)
                return;
            if (typeof item.focusHandle === "function")
                item.focusHandle(root.focusReason);
        }
    }

    Timer {
        interval: Timing.cappedTick
        repeat: true
        running: root.draggingIndex >= 0 && root.visible && !!root.flickable
        onTriggered: {
            const view = root.flickable;
            const point = root.mapToItem(view, 0, root.pointerY);
            const edge = Style.listItemHeight;
            const speed = point.y < edge ? -Math.min(1, (edge - point.y) / edge) : (point.y > view.height - edge ? Math.min(1, (point.y - view.height + edge) / edge) : 0);
            if (speed === 0)
                return;
            const before = view.contentY;
            view.contentY = Math.max(view.originY, Math.min(view.originY + Math.max(0, view.contentHeight - view.height), before + speed * Style.spacingM));
            const delta = view.contentY - before;
            if (delta !== 0)
                root.dragTo(Qt.point(0, root.pointerY + delta));
        }
    }
}
