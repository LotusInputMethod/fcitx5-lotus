import QtQuick
import DCommon.Common

DActionButton {
    id: root

    required property Item coordinateItem
    property string label: ""
    property bool upDownKeysMove: true
    property bool dragging: false
    property bool canMoveUp: true
    property bool canMoveDown: true

    signal started(point position)
    signal moved(point position)
    signal finished
    signal dragCanceled
    signal moveRequested(int delta)

    iconName: "drag_indicator"
    buttonSize: Style.iconButtonSize
    tooltipText: I18n.tr("Drag to reorder")
    Accessible.name: label
    Accessible.description: I18n.tr("Drag to reorder")
    Accessible.onIncreaseAction: {
        if (root.enabled && root.canMoveDown)
            root.moveRequested(1);
    }
    Accessible.onDecreaseAction: {
        if (root.enabled && root.canMoveUp)
            root.moveRequested(-1);
    }

    Keys.onPressed: event => {
        if (!upDownKeysMove && (event.key === Qt.Key_Up || event.key === Qt.Key_Down))
            return;
        switch (event.key) {
        case Qt.Key_Up:
            if (root.canMoveUp)
                root.moveRequested(-1);
            event.accepted = true;
            return;
        case Qt.Key_Down:
            if (root.canMoveDown)
                root.moveRequested(1);
            event.accepted = true;
            return;
        case Qt.Key_Escape:
            if (!root.dragging)
                return;
            root.dragCanceled();
            event.accepted = true;
            return;
        }
    }

    DragHandler {
        target: null
        xAxis.enabled: false
        cursorShape: active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onGrabChanged: (transition, point) => {
            switch (transition) {
            case PointerDevice.GrabExclusive:
                root.forceActiveFocus(Qt.MouseFocusReason);
                root.started(root.coordinateItem.mapFromItem(null, point.scenePressPosition.x, point.scenePressPosition.y));
                root.moved(root.coordinateItem.mapFromItem(null, point.scenePosition.x, point.scenePosition.y));
                return;
            case PointerDevice.UngrabExclusive:
                if (root.dragging)
                    root.finished();
                return;
            case PointerDevice.CancelGrabExclusive:
                root.dragCanceled();
                return;
            }
        }
        onCentroidChanged: {
            if (!active || !root.dragging)
                return;
            root.moved(root.coordinateItem.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y));
        }
    }
}
