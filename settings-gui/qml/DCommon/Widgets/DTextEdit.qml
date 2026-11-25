import QtQuick
import DCommon.Common

StyledRect {
    id: root

    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    property alias text: textEdit.text
    property alias cursorPosition: textEdit.cursorPosition
    property alias font: textEdit.font
    property alias textColor: textEdit.color
    property alias wrapMode: textEdit.wrapMode
    property alias readOnly: textEdit.readOnly
    property string placeholderText: ""
    property string leftIconName: ""
    property int leftIconSize: Style.iconSize
    property color leftIconColor: Style.onSurfaceVariant
    property color leftIconFocusedColor: Style.primary
    property bool usePopupTransparency: !Style.isFloatingWindow(root)
    property color backgroundColor: Style.chipSurface
    property color focusedBorderColor: Style.primary
    property color normalBorderColor: Style.outlineVariant
    property color placeholderColor: Style.onSurfaceVariant
    property bool hidePlaceholderOnFocus: true
    property real borderWidth: Style.outlineWidth
    property real focusedBorderWidth: Style.outlineWidthFocused
    property real cornerRadius: Style.cornerRadiusXS
    property real topPadding: Style.spacingS
    property real bottomPadding: Style.spacingS
    property var keyForwardTargets: []

    readonly property bool placeholderVisible: textEdit.text.length === 0 && !textEdit.inputMethodComposing && (!hidePlaceholderOnFocus || !textEdit.activeFocus)

    signal textEdited
    signal editingFinished
    signal focusStateChanged(bool hasFocus)

    function getActiveFocus() {
        return textEdit.activeFocus;
    }
    function setFocus(value) {
        textEdit.focus = value;
    }
    function forceActiveFocus() {
        textEdit.forceActiveFocus();
    }
    function selectAll() {
        textEdit.selectAll();
    }
    function clear() {
        textEdit.clear();
    }
    function insertText(str) {
        textEdit.insert(textEdit.cursorPosition, str);
    }

    width: Style.fieldDefaultWidth
    height: Style.textEditHeight
    radius: cornerRadius
    color: Style.foregroundColor(backgroundColor, !usePopupTransparency)
    border.color: textEdit.activeFocus ? focusedBorderColor : normalBorderColor
    border.width: textEdit.activeFocus ? focusedBorderWidth : borderWidth

    DIcon {
        id: leftIcon

        anchors.left: parent.left
        anchors.leftMargin: Style.spacingM
        anchors.top: parent.top
        anchors.topMargin: Style.spacingM
        name: leftIconName
        size: leftIconSize
        color: textEdit.activeFocus ? leftIconFocusedColor : leftIconColor
        visible: leftIconName !== ""
    }

    DFlickable {
        id: scroll

        function ensureCursorVisible() {
            if (height <= 0)
                return;
            const r = textEdit.cursorRectangle;
            if (r.y < contentY) {
                contentY = r.y;
                return;
            }
            if (r.y + r.height > contentY + height)
                contentY = r.y + r.height - height;
        }

        anchors.left: leftIcon.visible ? leftIcon.right : parent.left
        anchors.leftMargin: leftIcon.visible ? Style.spacingS : Style.spacingM
        anchors.right: parent.right
        anchors.rightMargin: Style.spacingM
        anchors.top: parent.top
        anchors.topMargin: root.topPadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomPadding
        clip: true
        contentWidth: width
        contentHeight: textEdit.height

        TextEdit {
            id: textEdit

            activeFocusOnTab: root.enabled
            Accessible.name: root.Accessible.name || root.placeholderText
            Accessible.description: root.Accessible.description

            width: scroll.width
            height: Math.max(scroll.height, contentHeight)
            font.pixelSize: Style.fontSizeMedium
            font.family: Style.fontFamily
            font.weight: Style.fontWeight
            color: Style.surfaceText
            selectionColor: Style.primary
            selectedTextColor: Style.onPrimary
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            cursorDelegate: DTextCursor {
                id: editCursor

                color: textEdit.color
                x: textEdit.cursorRectangle.x
                y: textEdit.cursorRectangle.y
                height: textEdit.cursorRectangle.height
                shown: textEdit.cursorVisible

                readonly property int editCursorPosition: textEdit.cursorPosition
                readonly property string editText: textEdit.text

                onEditCursorPositionChanged: resetBlink()
                onEditTextChanged: resetBlink()
            }
            onTextChanged: root.textEdited()
            onEditingFinished: root.editingFinished()
            onActiveFocusChanged: root.focusStateChanged(activeFocus)
            onCursorRectangleChanged: scroll.ensureCursorVisible()
            Keys.forwardTo: root.keyForwardTargets

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.IBeamCursor
                acceptedButtons: Qt.NoButton
            }
        }
    }

    StyledText {
        id: placeholderLabel

        anchors.fill: scroll
        text: root.placeholderText
        font: textEdit.font
        color: placeholderColor
        wrapMode: Text.WordWrap
        visible: root.placeholderVisible
    }

    Behavior on border.color {
        DColorAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    Behavior on border.width {
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }
}
