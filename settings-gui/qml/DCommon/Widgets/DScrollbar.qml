import QtQuick
import QtQuick.Controls
import DCommon.Common

ScrollBar {
    id: scrollbar

    property Flickable targetFlickable: null
    property bool allowed: true
    property real topMargin: 0

    property bool _scrollBarActive: false
    property bool _extentPrimed: false
    property alias hideTimer: hideScrollBarTimer
    readonly property Item scrollTarget: targetFlickable ?? parent
    readonly property bool scrollTargetMoving: !!scrollTarget && (scrollTarget.moving || scrollTarget.flicking || scrollTarget.isMomentumActive === true)
    readonly property bool shouldShow: pressed || hovered || active || scrollTargetMoving || _scrollBarActive
    readonly property bool scrollable: !!scrollTarget && scrollTarget.contentHeight > scrollTarget.height
    readonly property var placement: targetFlickable && policy !== ScrollBar.AlwaysOff ? placementFor(targetFlickable) : null
    readonly property real thumbX: placement && _extentPrimed ? thumbXFor(targetFlickable, placement.room) : 0

    function managesChildren(item) {
        return typeof item.positioningComplete === "function" || item.layoutDirection !== undefined || typeof item.itemAt === "function";
    }

    function isSurface(item) {
        return item.radius !== undefined && item.color !== undefined;
    }

    function placementFor(view) {
        let result = null;
        let room = 0;
        let x = 0;
        let y = 0;
        let opacity = 1;
        let item = view;
        while (item.parent) {
            const parent = item.parent;
            x += item.x;
            y += item.y;
            opacity *= item.opacity;
            const extent = mirrored ? x : parent.width - x - view.width;
            if (room === 0 && extent >= Style.scrollbarThickness)
                room = extent;
            if ((!result || room > 0) && !managesChildren(parent)) {
                result = {
                    "host": parent,
                    "anchor": item,
                    "x": x,
                    "y": y,
                    "opacity": opacity,
                    "room": room
                };
                if (room > 0)
                    return result;
            }
            if (room === 0 && isSurface(parent))
                return result;
            item = parent;
        }
        return result;
    }

    function thumbXFor(view, room) {
        let lead = view.width;
        let trail = 0;
        if (typeof view.itemAtIndex === "function") {
            const rect = view.contentItem.childrenRect;
            if (rect.width > 0) {
                lead = view.contentItem.x + rect.x;
                trail = lead + rect.width;
            }
        } else {
            for (const child of view.contentItem.children) {
                if (!child.visible || child.width <= 0)
                    continue;
                const padded = typeof child.positioningComplete === "function";
                lead = Math.min(lead, view.contentItem.x + child.x + (padded ? child.leftPadding : 0));
                trail = Math.max(trail, view.contentItem.x + child.x + child.width - (padded ? child.rightPadding : 0));
            }
        }
        lead = Math.max(0, Math.min(lead, view.width));
        trail = trail > 0 ? Math.min(trail, view.width) : view.width;
        const thickness = Style.scrollbarThickness;
        const gutter = mirrored ? lead + room : view.width + room - trail;
        if (gutter < thickness)
            return mirrored ? Style.spacingXXS - room : view.width + room - thickness - Style.spacingXXS;
        const gap = Math.min(Style.scrollbarGap, (gutter - thickness) / 2);
        return mirrored ? lead - gap - thickness : trail + gap;
    }

    Component.onCompleted: {
        if (typeof targetFlickable?.itemAtIndex === "function")
            void targetFlickable.contentItem.childrenRect.width;
        _extentPrimed = true;
    }

    Binding on parent {
        when: scrollbar.targetFlickable !== null
        value: scrollbar.placement?.host ?? scrollbar.targetFlickable
    }

    Binding on x {
        when: scrollbar.placement !== null
        value: scrollbar.placement ? scrollbar.placement.x + scrollbar.thumbX - scrollbar.leftPadding : 0
    }

    Binding on height {
        when: scrollbar.targetFlickable !== null
        value: scrollbar.targetFlickable?.height ?? 0
    }

    y: placement?.y ?? 0
    z: placement?.anchor.z ?? 0
    policy: Style.scrollbarsEnabled && allowed && scrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    minimumSize: 0.08
    padding: 0
    leftPadding: Style.scrollbarGap
    rightPadding: Style.scrollbarGap
    topPadding: Style.spacingXXS + topMargin
    bottomPadding: Style.spacingXXS
    interactive: true
    hoverEnabled: true
    enabled: scrollTarget?.enabled ?? false
    visible: policy !== ScrollBar.AlwaysOff && (scrollTarget?.visible ?? false)
    opacity: policy !== ScrollBar.AlwaysOff && shouldShow ? (placement?.opacity ?? 1) : 0

    Behavior on opacity {
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    contentItem: Rectangle {
        implicitWidth: Style.scrollbarThickness
        radius: Style.fullRadius(width, height)
        color: scrollbar.pressed ? Style.primary : Style.outline
    }

    background: Item {}

    Timer {
        id: hideScrollBarTimer
        interval: Style.scrollbarHideDelay
        onTriggered: scrollbar._scrollBarActive = false
    }
}
