import QtQuick
import DCommon.Common

Item {
    id: root

    property var controls: null
    property string title: ""
    property real titleFontSize: Style.fontSizeLarge
    property int titleWeight: Style.fontWeightMedium
    property int titleAlignment: Text.AlignHCenter
    property bool wrapTitle: false
    property real horizontalPadding: -1
    property real verticalPadding: Style.windowInset
    property bool showDivider: false
    property string subtitle: ""
    property string iconName: ""
    property bool closeEnabled: true
    property string closeTooltipText: ""
    default property alias actions: extraActions.data

    readonly property bool compositorDecorated: controls !== null && !Host.ownWindowDecorations
    readonly property bool hasActions: extraActions.implicitWidth > 0
    readonly property bool collapsed: compositorDecorated && !hasActions
    readonly property bool centered: titleAlignment === Text.AlignHCenter
    readonly property real edgeInset: horizontalPadding >= 0 ? horizontalPadding : Style.windowInset
    readonly property real titleGap: horizontalPadding >= 0 ? horizontalPadding : Style.spacingM
    readonly property real buttonsReserve: buttons.width + edgeInset + titleGap

    signal closeRequested

    implicitHeight: collapsed ? verticalPadding : Math.max(buttons.implicitHeight, titleColumn.implicitHeight) + verticalPadding * 2
    height: implicitHeight
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    component WindowButton: DActionButton {
        buttonSize: Style.buttonHeightXS
        iconSize: Style.iconSizeSmall
        iconColor: Style.onSurfaceVariant
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: buttons.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: Style.spacingM
        enabled: root.controls !== null && !root.compositorDecorated
        onPressed: root.controls.tryStartMove()
        onDoubleClicked: root.controls.tryToggleMaximize()
    }

    Column {
        id: titleColumn
        width: root.centered ? Math.max(0, root.width - 2 * Math.max(root.edgeInset, root.buttonsReserve)) : Math.max(0, root.width - root.edgeInset - root.buttonsReserve)
        x: root.centered ? (root.width - width) / 2 : (LayoutMirroring.enabled ? root.buttonsReserve : root.edgeInset)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.spacingXS
        visible: !root.compositorDecorated

        StyledText {
            width: parent.width
            text: root.title
            font.pixelSize: root.titleFontSize
            font.weight: root.titleWeight
            color: Style.surfaceText
            horizontalAlignment: root.titleAlignment
            elide: root.wrapTitle ? Text.ElideNone : Text.ElideRight
            wrapMode: root.wrapTitle ? Text.Wrap : Text.NoWrap
        }

        StyledText {
            width: parent.width
            text: root.subtitle
            font.pixelSize: Style.fontSizeSmall
            color: Style.surfaceTextMedium
            horizontalAlignment: root.titleAlignment
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            visible: text !== ""
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: root.edgeInset
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.windowInset

        Row {
            id: extraActions
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.windowInset
        }

        WindowButton {
            objectName: "minimizeWindow"
            visible: !root.compositorDecorated && (root.controls?.canMinimize ?? false)
            iconName: "minimize"
            Accessible.name: I18n.tr("Minimize")
            onClicked: root.controls.tryMinimize()
        }

        WindowButton {
            objectName: "maximizeWindow"
            visible: !root.compositorDecorated && (root.controls?.canMaximize ?? false)
            iconName: root.controls?.targetWindow?.maximized ? "fullscreen_exit" : "fullscreen"
            Accessible.name: root.controls?.targetWindow?.maximized ? I18n.tr("Restore") : I18n.tr("Maximize")
            onClicked: root.controls.tryToggleMaximize()
        }

        WindowButton {
            objectName: "closeWindow"
            visible: !root.compositorDecorated
            enabled: root.closeEnabled
            iconName: "close"
            tooltipText: root.closeTooltipText || null
            Accessible.name: root.closeTooltipText || I18n.tr("Close")
            onClicked: root.closeRequested()
        }
    }
}
