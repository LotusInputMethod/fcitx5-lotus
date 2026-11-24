import QtQuick
import Quickshell.Widgets
import DCommon.Common

FocusScope {
    id: card

    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    property int pad: Style.spacingM
    property bool clickable: false
    property bool interactive: true
    property string title: ""
    property string tone: ""
    property alias color: surface.color
    property alias radius: surface.radius
    property alias border: surface.border
    default property alias content: contentItem.data

    signal clicked

    readonly property Item focusTarget: input
    readonly property var focusTargets: acceptsInput ? [input] : []

    readonly property bool acceptsInput: clickable && interactive && enabled
    readonly property bool floatingWindow: Style.isFloatingWindow(card)
    readonly property bool tinted: toneColors.tinted
    readonly property color containerColor: toneColors.containerColor
    readonly property color contentColor: toneColors.contentColor
    readonly property color accentColor: toneColors.accentColor
    property color onAccentColor
    readonly property color mutedColor: toneColors.mutedColor
    readonly property color chipColor: toneColors.chipColor
    readonly property color surfaceColor: toneColors.surfaceColor
    property bool showFocusRing: true
    property bool clipContent: false
    property real restRadius: Style.cornerRadiusM
    property real bodyRadius: restRadius

    readonly property real _contentImplicitHeight: {
        let h = 0;
        for (let i = 0; i < contentItem.children.length; i++) {
            const c = contentItem.children[i];
            if (c && c.visible)
                h = Math.max(h, c.implicitHeight > 0 ? c.implicitHeight : c.height);
        }
        return h;
    }

    implicitHeight: _contentImplicitHeight + pad * 2 + (titleLabel.visible ? titleLabel.implicitHeight + Style.spacingXS : 0)
    radius: bodyRadius
    color: surfaceColor
    border.width: Style.layerOutlineWidth
    border.color: Style.outlineMedium
    activeFocusOnTab: acceptsInput
    Accessible.role: Accessible.Pane
    Accessible.name: title

    DTone {
        id: toneColors
        tone: card.tone
        floatingWindow: card.floatingWindow
    }

    Binding {
        target: card
        property: "onAccentColor"
        value: toneColors.onAccentColor
    }

    Behavior on bodyRadius {
        enabled: !Style.reduceMotion
        DAnim {
            duration: Style.expressiveDurations.expressiveFastSpatial
            easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
        }
    }

    Rectangle {
        id: surface
        anchors.fill: parent
    }

    StyledButton {
        id: input
        focus: true
        anchors.fill: parent
        enabled: card.acceptsInput
        visible: card.clickable
        Accessible.name: card.Accessible.name
        Accessible.description: card.Accessible.description
        Accessible.ignored: card.Accessible.ignored
        onClicked: card.clicked()
    }

    StateLayer {
        control: input
        visible: card.acceptsInput
        disabled: !card.acceptsInput
        stateColor: card.accentColor
    }

    FocusRing {
        visible: card.showFocusRing && input.visualFocus
    }

    StyledText {
        id: titleLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: card.pad
        text: card.title
        font.pixelSize: Style.fontSizeMedium
        font.weight: Style.fontWeightMedium
        color: card.accentColor
        elide: Text.ElideRight
        visible: text !== ""
    }

    Item {
        id: contentHost
        anchors.fill: parent
        opacity: card.interactive ? 1 : 0.45
        scale: card.interactive ? 1 : 0.92
        transformOrigin: Item.Center

        Loader {
            id: clipLoader
            anchors.fill: parent
            active: card.clipContent
            sourceComponent: ClippingRectangle {
                radius: card.bodyRadius
                color: "transparent"
            }
        }

        Item {
            id: contentItem
            parent: clipLoader.item ?? contentHost
            anchors.fill: parent
            anchors.margins: card.pad
            anchors.topMargin: card.pad + (titleLabel.visible ? titleLabel.height + Style.spacingXS : 0)
        }

        Behavior on opacity {
            enabled: !Style.reduceMotion
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        Behavior on scale {
            enabled: !Style.reduceMotion
            DAnim {
                duration: Style.expressiveDurations.expressiveFastSpatial
                easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
            }
        }
    }
}
