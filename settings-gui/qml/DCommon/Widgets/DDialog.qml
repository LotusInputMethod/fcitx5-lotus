import QtQuick
import QtQuick.Window
import DCommon.Common
import "../Common/FocusNavigation.js" as FocusNavigation

FocusScope {
    id: root

    property string title: ""
    property string iconName: ""
    property string supportingText: ""
    property var windowControls: null
    property bool closeEnabled: true
    property bool acceptEnabled: true
    property bool embedded: true
    property bool popout: false
    property bool opened: true
    readonly property bool nativeWindow: windowControls !== null
    property real contentSpacing: nativeWindow || popout ? Style.spacingM : Style.spacingL
    property real actionSpacing: nativeWindow || popout ? contentSpacing : Style.spacingXL
    property real padding: nativeWindow || popout ? Style.spacingL : Style.spacingXL
    property real maximumWidth: Style.dialogMaxWidth
    property real maximumHeight: Infinity
    property color surfaceColor: nativeWindow ? Style.cardSurface : Style.hostSurface
    property real surfaceRadius: nativeWindow ? Style.windowRadius : Style.cornerRadiusXL
    default property alias content: body.data
    property alias actions: actionFlow.data
    property alias leftActions: leftActionFlow.data
    property alias headerActions: header.actions
    readonly property alias contentItem: body
    readonly property real actionWidth: surface.width - padding * 2
    readonly property real contentHeight: scrollBody.implicitHeight
    readonly property real focusPadding: Style.focusRingOffset + Style.focusRingWidth
    readonly property real availableContentHeight: Math.max(0, scroll.height - focusPadding * 2)
    readonly property real bodyTop: popout ? padding : header.y + header.height + contentSpacing
    readonly property bool active: embedded || opened || surface.opacity > 0

    signal accepted
    signal rejected

    implicitWidth: maximumWidth
    implicitHeight: bodyTop + scrollBody.implicitHeight + Math.max(actionFlow.implicitHeight, leftActionFlow.implicitHeight) + padding + (Math.max(actionFlow.implicitHeight, leftActionFlow.implicitHeight) > 0 ? actionSpacing : 0)
    focus: embedded || opened
    Accessible.role: Accessible.Dialog
    Accessible.name: title
    Accessible.description: supportingText
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    Keys.onPressed: event => {
        if (event.isAutoRepeat || (!root.embedded && !root.opened))
            return;
        switch (event.key) {
        case Qt.Key_Escape:
            if (!root.closeEnabled)
                return;
            root.rejected();
            event.accepted = true;
            return;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!root.acceptEnabled)
                return;
            root.accepted();
            event.accepted = true;
            return;
        }
    }

    function revealFocus() {
        const item = root.Window.window?.activeFocusItem;
        if (!item || !root.visible)
            return;
        let target = item;
        let ancestor = item;
        while (ancestor && ancestor !== body) {
            if (typeof ancestor.getActiveFocus === "function")
                target = ancestor;
            ancestor = ancestor.parent;
        }
        if (!ancestor)
            return;
        const point = target.mapToItem(scroll.contentItem, 0, 0);
        scroll.revealRange(point.y - root.focusPadding, point.y + target.height + root.focusPadding);
    }

    readonly property Item windowActiveFocusItem: root.Window.window?.activeFocusItem ?? null
    onWindowActiveFocusItemChanged: {
        revealFocus();
        if (!overlayOpen)
            return;
        if (FocusNavigation.keepsFocus(root, windowActiveFocusItem))
            lastFocused = windowActiveFocusItem;
        else
            Qt.callLater(reclaimFocus);
    }

    readonly property bool overlayOpen: !embedded && opened && visible
    property real overlayOpenedAt: 0
    property Item lastFocused: null
    onOverlayOpenChanged: {
        overlayOpenedAt = overlayOpen ? Date.now() : 0;
        lastFocused = null;
    }

    function reclaimFocus() {
        const item = windowActiveFocusItem;
        if (!overlayOpen || !item || FocusNavigation.keepsFocus(root, item))
            return;
        const stops = FocusNavigation.tabStops(surface);
        const target = lastFocused === stops[0] ? stops[stops.length - 1] : stops[0];
        (target ?? root).forceActiveFocus(Qt.TabFocusReason);
    }

    data: [
        Rectangle {
            anchors.fill: parent
            visible: !root.embedded && (root.opened || opacity > 0)
            color: Style.backdropColor
            opacity: root.opened ? Style.scrimAlpha : 0

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
                onClicked: {
                    if (root.closeEnabled)
                        root.rejected();
                }
            }

            Behavior on opacity {
                enabled: !Style.reduceMotion
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }
        },
        Item {
            id: surface

            anchors.centerIn: parent
            width: root.embedded ? parent.width : Math.max(0, Math.min(root.maximumWidth, parent.width - root.padding * 2))
            height: root.embedded ? parent.height : Math.max(0, Math.min(root.implicitHeight, root.maximumHeight, parent.height - root.padding * 2))
            visible: root.embedded || root.opened || opacity > 0
            enabled: root.embedded || root.opened
            property real entryScale: root.embedded || root.opened ? 1 : Style.popupEnterScale
            scale: Math.min(1, entryScale)
            opacity: root.embedded || root.opened ? 1 : 0

            Behavior on entryScale {
                enabled: !root.embedded && !Style.reduceMotion
                DAnim {
                    duration: Style.expressiveDurations.expressiveDefaultSpatial
                    easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
                }
            }
            Behavior on opacity {
                enabled: !root.embedded && !Style.reduceMotion
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }

            ElevationShadow {
                anchors.fill: parent
                visible: !root.embedded
                level: Style.elevationLevel3
                targetRadius: root.surfaceRadius
                targetColor: root.surfaceColor
                borderWidth: Style.layerOutlineWidth
                borderColor: Style.outlineMedium
                shadowEnabled: Style.elevationEnabled
            }

            MouseArea {
                anchors.fill: parent
                enabled: !root.embedded
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
            }

            DWindowHeader {
                id: header

                x: root.nativeWindow ? 0 : root.padding
                y: root.nativeWindow ? 0 : root.padding
                width: parent.width - (root.nativeWindow ? 0 : root.padding * 2)
                visible: !root.popout
                controls: root.windowControls
                title: root.title
                titleAlignment: root.nativeWindow ? Text.AlignHCenter : Text.AlignLeft
                titleFontSize: root.nativeWindow ? Style.fontSizeMedium : Style.fontSizeXLarge
                titleWeight: root.nativeWindow ? Style.fontWeightMedium : Style.fontWeight
                wrapTitle: !root.nativeWindow
                horizontalPadding: root.nativeWindow ? -1 : 0
                verticalPadding: root.nativeWindow ? Style.windowInset : Style.spacingXS
                closeEnabled: root.closeEnabled
                onCloseRequested: root.rejected()
            }

            DFlickable {
                id: scroll

                readonly property real footerHeight: Math.max(actionFlow.height, leftActionFlow.height)
                readonly property real footerTop: Math.min(actionFlow.y, leftActionFlow.y)

                x: root.padding - root.focusPadding
                y: root.bodyTop - root.focusPadding
                width: parent.width - root.padding * 2 + root.focusPadding * 2
                height: Math.max(0, footerTop - y - (footerHeight > 0 ? root.actionSpacing : 0) + root.focusPadding)
                contentWidth: width
                contentHeight: scrollBody.implicitHeight + root.focusPadding * 2
                clip: true

                Column {
                    id: scrollBody
                    x: root.focusPadding
                    y: root.focusPadding
                    width: scroll.width - root.focusPadding * 2
                    spacing: root.contentSpacing

                    StyledText {
                        width: parent.width
                        text: root.supportingText
                        color: Style.onSurfaceVariant
                        font.pixelSize: Style.fontSizeMedium
                        wrapMode: Text.Wrap
                        visible: text.length > 0
                    }

                    Column {
                        id: body
                        width: parent.width
                        spacing: root.contentSpacing
                    }
                }
            }

            Flow {
                id: leftActionFlow

                readonly property real naturalWidth: children.reduce((total, item) => total + (item.visible ? item.width + spacing : 0), -spacing)
                width: Math.min(Math.max(0, naturalWidth), parent.width - root.padding * 2)
                anchors.left: parent.left
                anchors.leftMargin: root.padding
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.padding
                spacing: Style.spacingS
            }

            Flow {
                id: actionFlow

                readonly property real naturalWidth: children.reduce((total, item) => total + (item.visible ? item.width + spacing : 0), -spacing)
                width: Math.min(Math.max(0, naturalWidth), parent.width - root.padding * 2)
                anchors.right: parent.right
                anchors.rightMargin: root.padding
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.padding
                spacing: Style.spacingS
            }
        }
    ]
}
