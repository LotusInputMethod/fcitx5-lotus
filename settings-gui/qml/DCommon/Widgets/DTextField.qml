import QtQuick
import QtQuick.Templates as T
import DCommon.Common

StyledRect {
    id: root

    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    KeyNavigation.tab: keyNavigationTab
    KeyNavigation.backtab: keyNavigationBacktab

    onActiveFocusChanged: {
        if (!activeFocus)
            return;
        forceActiveFocus();
    }

    property alias text: textInput.text
    property alias cursorPosition: textInput.cursorPosition
    property string placeholderText: ""
    property string labelText: ""
    property bool alwaysFloatLabel: false
    property bool outlined: false
    property bool isError: false
    property string supportingText: ""
    property alias readOnly: textInput.readOnly
    property alias font: textInput.font
    property alias textColor: textInput.color
    property int echoMode: TextInput.Normal
    property alias validator: textInput.validator
    property alias maximumLength: textInput.maximumLength
    property string leftIconName: ""
    property int leftIconSize: Style.iconSize
    property color leftIconColor: Style.onSurfaceVariant
    property color leftIconFocusedColor: Style.primary
    property Component leadingContent: null
    property bool showClearButton: false
    property bool showPasswordToggle: false
    property real rightAccessoryWidth: 0
    property bool passwordVisible: false
    readonly property bool imeShielded: echoMode === TextInput.Password && !passwordVisible && !textInput.readOnly
    readonly property bool inputFocused: textInput.activeFocus || imeSink.activeFocus
    readonly property bool composing: textInput.inputMethodComposing || imeSink.inputMethodComposing
    property bool usePopupTransparency: !Style.isFloatingWindow(root)
    property color backgroundColor: Style.chipSurface
    property color focusedBorderColor: expressive ? Style.focusRingColor : Style.primary
    property color normalBorderColor: expressive ? Style.outlineMedium : outlined ? Style.outline : Style.outlineVariant
    property color placeholderColor: Style.onSurfaceVariant
    property bool hidePlaceholderOnFocus: true
    property real borderWidth: expressive ? Style.layerOutlineWidth : Style.outlineWidth
    property real focusedBorderWidth: expressive ? (Style.focusRingWidth > 0 ? Math.max(Style.outlineWidth, Style.focusRingWidth) : Style.layerOutlineWidth) : Style.outlineWidthFocused
    property real cornerRadius: expressive ? Style.cornerRadiusXL : Style.cornerRadiusXS
    property real controlHeight: expressive ? Style.buttonHeightM : Style.iconButtonSize
    property real fieldHeight: Style.fieldHeight
    property bool expressive: false
    property bool morph: false
    property bool busy: false
    property bool showAcceptButton: false
    property string acceptButtonName: ""
    property Component trailingContent: null
    property real shakeOffset: 0
    property int typedCount: 0
    readonly property real morphSize: controlHeight - Style.spacingS * 2
    readonly property bool morphShown: expressive && morph
    readonly property int spatialDuration: Style.reduceMotion ? 0 : Style.expressiveDurations.expressiveFastSpatial
    readonly property int settleDuration: Style.reduceMotion ? 0 : Style.expressiveDurations.expressiveDefaultSpatial
    readonly property int effectsDuration: Style.reduceMotion ? 0 : Style.expressiveDurations.expressiveEffects

    readonly property real accessorySize: outlined ? Math.min(controlHeight, Style.fieldHeightLarge) : Style.buttonHeightXS
    readonly property real contentPadding: outlined ? Style.spacingL : Style.spacingM
    readonly property real leftPadding: {
        if (morphShown)
            return Style.spacingS + morphSize + Style.spacingM;
        if (leadingLoader.item)
            return contentPadding + leadingLoader.width + Style.spacingS;
        if (outlined && leftIconName)
            return accessorySize + Style.spacingXS;
        return contentPadding + (leftIconName ? leftIconSize + contentPadding : 0);
    }
    readonly property real rightPadding: {
        let p = Style.spacingS + rightAccessoryWidth;
        if (showPasswordToggle)
            p += accessorySize + Style.spacingXS;
        if (showClearButton && text.length > 0)
            p += accessorySize + Style.spacingXS;
        return p;
    }
    property real topPadding: Style.spacingS
    property real bottomPadding: Style.spacingS
    property bool ignoreLeftRightKeys: false
    property bool ignoreUpDownKeys: false
    property bool ignoreTabKeys: false
    property var keyForwardTargets: []
    property Item keyNavigationTab: null
    property Item keyNavigationBacktab: null

    signal textEdited
    signal editingFinished
    signal cleared
    signal accepted
    signal focusStateChanged(bool hasFocus)

    function getActiveFocus() {
        return inputFocused;
    }
    function setFocus(value) {
        textInput.focus = value;
    }
    function forceActiveFocus() {
        (imeShielded ? imeSink : textInput).forceActiveFocus();
    }
    function selectAll() {
        textInput.selectAll();
    }
    function select(start, end) {
        textInput.select(start, end);
    }
    function clear() {
        textInput.clear();
    }
    function insertText(str) {
        textInput.insert(textInput.cursorPosition, str);
    }
    function backspace() {
        if (textInput.selectedText.length > 0) {
            textInput.remove(textInput.selectionStart, textInput.selectionEnd);
            return;
        }
        if (textInput.cursorPosition === 0)
            return;
        textInput.remove(textInput.cursorPosition - 1, textInput.cursorPosition);
    }
    function shake() {
        if (!shakeLoader.item)
            return;
        shakeLoader.item.restart();
    }

    readonly property real labelBandHeight: Math.round(Style.fontSizeSmall * 1.4) + Style.spacingXS * 2
    readonly property bool labelFloated: root.alwaysFloatLabel || root.inputFocused || text.length > 0 || root.composing
    readonly property real labelProgress: Math.max(0, Math.min(1, labelMotion.value))
    readonly property real containerTop: outlined && labelText ? Style.outlinedFieldLabelLineHeight / 2 : 0
    readonly property string shownPlaceholderText: expressive && labelText ? labelText : placeholderText
    readonly property string shownSupportingText: supportingText || (expressive && labelText ? placeholderText : "")
    readonly property real supportingHeight: shownSupportingText ? supportingLabel.implicitHeight + Style.spacingXS : 0
    readonly property real containerHeight: height - containerTop - supportingHeight
    readonly property bool placeholderVisible: textInput.text.length === 0 && !root.composing && (outlined ? (!labelText || labelFloated) : expressive || !hidePlaceholderOnFocus || !root.inputFocused)
    readonly property color outlineTargetColor: !enabled ? Style.onSurface_12 : isError ? (fieldHover.hovered && !root.inputFocused ? Style.onErrorContainer : Style.error) : root.inputFocused ? focusedBorderColor : fieldHover.hovered ? Style.onSurface : normalBorderColor
    readonly property real outlineStrokeWidth: !enabled ? borderWidth : borderWidth + (focusedBorderWidth - borderWidth) * Math.max(0, Math.min(1, strokeMotion.value))
    readonly property color labelTargetColor: !enabled ? Style.onSurface_38 : isError ? (fieldHover.hovered && !root.inputFocused ? Style.onErrorContainer : Style.error) : root.inputFocused ? Style.primary : fieldHover.hovered ? Style.onSurface : Style.onSurfaceVariant

    width: Style.fieldDefaultWidth
    implicitHeight: outlined ? Math.max(controlHeight, textInput.contentHeight + topPadding + bottomPadding) + containerTop + supportingHeight : expressive ? controlHeight + supportingHeight : fieldHeight + (labelText ? labelBandHeight : 0)
    height: implicitHeight
    radius: cornerRadius
    color: outlined || expressive ? "transparent" : Style.foregroundColor(backgroundColor, !usePopupTransparency)
    border.color: isError ? Style.error : root.inputFocused ? focusedBorderColor : normalBorderColor
    border.width: outlined || expressive ? 0 : root.inputFocused || isError ? focusedBorderWidth : borderWidth

    Component.onCompleted: {
        labelMotion.snapTo(labelFloated ? 1 : 0);
        strokeMotion.snapTo(root.inputFocused ? 1 : 0);
        placeholderMotion.snapTo(placeholderVisible ? 1 : 0);
    }
    onLabelFloatedChanged: labelMotion.retarget(labelFloated ? 1 : 0)
    onInputFocusedChanged: {
        strokeMotion.retarget(inputFocused ? 1 : 0);
        focusStateChanged(inputFocused);
    }
    onImeShieldedChanged: {
        if (imeShielded && textInput.activeFocus) {
            imeSink.forceActiveFocus();
            return;
        }
        if (!imeShielded && imeSink.activeFocus)
            textInput.forceActiveFocus();
    }
    onTextChanged: {
        const grew = text.length > typedCount;
        typedCount = text.length;
        if (grew && morphLoader.item)
            morphLoader.item.pulse();
    }
    transform: Translate {
        x: root.shakeOffset
    }
    onEnabledChanged: {
        if (!enabled)
            strokeMotion.snapTo(0);
    }

    SpringMotion {
        id: labelMotion
        enabled: root.outlined
        stiffness: Style.springPreset("fastSpatial").stiffness
        damping: Style.springPreset("fastSpatial").damping
    }

    SpringMotion {
        id: strokeMotion
        enabled: root.outlined && root.enabled
        stiffness: labelMotion.stiffness
        damping: labelMotion.damping
    }

    HoverHandler {
        id: fieldHover
        enabled: root.enabled
        cursorShape: Qt.IBeamCursor
    }

    component FieldColor: QtObject {
        id: colorMotion
        property color target
        property color startColor: target
        property color endColor: target
        readonly property color value: Style._blend(startColor, endColor, Math.max(0, Math.min(1, progress.value)))
        onTargetChanged: {
            startColor = value;
            endColor = target;
            progress.snapTo(0);
            progress.retarget(1);
        }
        property SpringMotion progress: SpringMotion {
            value: 1
            enabled: root.outlined && root.enabled
            stiffness: Style.springPreset("fastEffects").stiffness
            damping: Style.springPreset("fastEffects").damping
        }
    }

    FieldColor {
        id: outlinePaint
        target: root.outlineTargetColor
    }
    FieldColor {
        id: labelPaint
        target: root.labelTargetColor
    }
    readonly property color outlineColor: outlinePaint.value
    readonly property color labelColor: labelPaint.value
    readonly property real cutoutCenter: fieldLabel.x + fieldLabel.width / 2
    readonly property real cutoutHalfWidth: labelText ? (fieldLabel.width / 2 + Style.spacingXS) * labelProgress : 0
    readonly property real cutoutStart: Math.max(0, cutoutCenter - cutoutHalfWidth)
    readonly property real cutoutEnd: Math.min(width, cutoutCenter + cutoutHalfWidth)
    readonly property real labelTop: fieldLabel.y + (fieldLabel.height - labelGlyph.height * fieldLabel.textScale) / 2
    readonly property real cutoutBottom: containerTop + (labelText ? Math.max(0, containerTop + outlineStrokeWidth - labelTop) : 0)

    component FieldOutline: Rectangle {
        x: -parent.x
        y: root.containerTop - parent.y
        width: root.width
        height: root.containerHeight
        color: "transparent"
        radius: root.cornerRadius
        border.color: root.outlineColor
        border.width: root.outlineStrokeWidth
        antialiasing: true
    }

    Loader {
        active: root.expressive
        visible: active
        width: root.width
        height: root.containerHeight
        sourceComponent: Rectangle {
            readonly property bool errorOutline: root.isError && !root.morph

            radius: root.cornerRadius
            color: Style.foregroundColor(root.backgroundColor, !root.usePopupTransparency)
            border.color: errorOutline ? Style.error : root.inputFocused ? root.focusedBorderColor : root.normalBorderColor
            border.width: errorOutline ? Style.outlineWidth : root.inputFocused ? root.focusedBorderWidth : root.borderWidth

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
    }

    Item {
        width: root.cutoutStart
        height: root.cutoutBottom
        clip: true
        visible: root.outlined
        FieldOutline {}
    }

    Item {
        x: root.cutoutEnd
        width: root.width - x
        height: root.cutoutBottom
        clip: true
        visible: root.outlined
        FieldOutline {}
    }

    Item {
        y: root.cutoutBottom
        width: root.width
        height: Math.max(0, root.containerTop + root.containerHeight - y)
        clip: true
        visible: root.outlined
        FieldOutline {}
    }

    MouseArea {
        anchors.fill: parent
        enabled: (root.outlined || root.expressive) && root.enabled
        onPressed: root.forceActiveFocus()
    }

    Loader {
        id: morphLoader

        anchors.left: parent.left
        anchors.leftMargin: Style.spacingS
        anchors.verticalCenter: textInput.verticalCenter
        active: root.morphShown
        visible: active
        width: root.morphSize
        height: root.morphSize
        sourceComponent: Item {
            function pulse() {
                morphPulse.restart();
            }

            Rectangle {
                id: morphShape

                anchors.fill: parent
                radius: width / 2
                color: root.isError ? Style.errorContainer : Style.primaryContainer

                SequentialAnimation {
                    id: morphPulse
                    NumberAnimation {
                        target: morphShape
                        property: "scale"
                        to: 1.15
                        duration: root.spatialDuration / 2
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
                    }
                    NumberAnimation {
                        target: morphShape
                        property: "scale"
                        to: 1
                        duration: root.settleDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
                    }
                }

                Behavior on color {
                    DColorAnim {
                        duration: root.effectsDuration
                        easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                    }
                }
            }

            Loader {
                anchors.fill: parent
                active: root.busy
                sourceComponent: DSpinner {
                    size: root.morphSize
                    color: root.isError ? Style.onErrorContainer : Style.onPrimaryContainer
                    running: root.busy && root.visible
                }
            }

            DIcon {
                anchors.centerIn: parent
                name: root.leftIconName
                size: Style.iconSizeSmall
                color: root.isError ? Style.onErrorContainer : Style.onPrimaryContainer
                visible: !root.busy && root.leftIconName !== ""
            }
        }
    }

    DIcon {
        id: leftIcon

        anchors.left: parent.left
        anchors.leftMargin: root.outlined ? (root.accessorySize - root.leftIconSize) / 2 : root.contentPadding
        anchors.verticalCenter: textInput.verticalCenter
        name: leftIconName
        size: leftIconSize
        color: root.outlined ? (root.enabled ? root.leftIconColor : Style.onSurface_38) : root.inputFocused ? leftIconFocusedColor : leftIconColor
        visible: leftIconName !== "" && !leadingLoader.item && !root.morphShown
    }

    Loader {
        id: leadingLoader

        anchors.left: parent.left
        anchors.leftMargin: root.contentPadding
        anchors.verticalCenter: textInput.verticalCenter
        active: root.leadingContent !== null && !root.morphShown
        sourceComponent: root.leadingContent
    }

    Item {
        id: fieldLabel

        readonly property real textScale: root.outlined ? 1 + (Style.fontSizeSmall / labelGlyph.font.pixelSize - 1) * root.labelProgress : 1

        anchors.left: parent.left
        anchors.leftMargin: root.outlined ? root.contentPadding + (root.leftPadding - root.contentPadding) * (1 - root.labelProgress) : root.leftPadding
        y: root.outlined ? root.containerTop + (root.containerHeight / 2) * (1 - root.labelProgress) - height / 2 : Style.spacingXS
        implicitWidth: labelGlyph.width * textScale
        width: implicitWidth
        height: root.outlined ? root.font.pixelSize + Style.spacingS + (Style.outlinedFieldLabelLineHeight - root.font.pixelSize - Style.spacingS) * root.labelProgress : Style.outlinedFieldLabelLineHeight
        visible: root.labelText !== "" && !root.expressive

        StyledText {
            id: labelGlyph

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.alignWhenCentered: false
            width: implicitWidth
            text: root.labelText
            font.pixelSize: root.outlined ? root.font.pixelSize : Style.fontSizeSmall
            scale: fieldLabel.textScale
            transformOrigin: I18n.isRtl ? Item.Right : Item.Left
            color: root.outlined ? root.labelColor : root.inputFocused ? Style.primary : Style.onSurfaceVariant
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideNone
        }
    }

    TextInput {
        id: imeSink

        x: textInput.x + textInput.cursorRectangle.x
        y: textInput.y + textInput.cursorRectangle.y
        width: 1
        height: textInput.cursorRectangle.height
        opacity: 0
        activeFocusOnPress: false
        activeFocusOnTab: root.enabled && root.imeShielded
        Accessible.ignored: true
        cursorDelegate: Item {}
        echoMode: TextInput.NoEcho
        inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        Keys.forwardTo: [textInput]
        onTextChanged: {
            if (text.length === 0)
                return;
            const committed = text;
            text = "";
            if (textInput.selectedText.length > 0)
                textInput.remove(textInput.selectionStart, textInput.selectionEnd);
            textInput.insert(textInput.cursorPosition, committed);
        }
    }

    T.TextField {
        id: textInput

        background: null
        padding: 0

        anchors.left: parent.left
        anchors.leftMargin: root.leftPadding
        anchors.right: rightButtonsRow.visible ? rightButtonsRow.left : parent.right
        anchors.rightMargin: rightButtonsRow.visible ? Style.spacingS : root.contentPadding + root.rightAccessoryWidth
        anchors.top: parent.top
        anchors.topMargin: root.outlined ? root.containerTop + root.topPadding : root.labelText !== "" && !root.expressive ? root.labelBandHeight : root.topPadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomPadding + (root.outlined || root.expressive ? root.supportingHeight : 0)
        font.pixelSize: Style.fontSizeMedium
        font.family: Style.fontFamily
        font.weight: Style.fontWeight
        color: root.outlined && !root.enabled ? Style.onSurface_38 : Style.surfaceText
        selectionColor: Style.primary
        selectedTextColor: Style.onPrimary
        horizontalAlignment: TextInput.AlignLeft
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        echoMode: root.passwordVisible && root.echoMode === TextInput.Password ? TextInput.Normal : root.echoMode
        passwordCharacter: Style.passwordMaskCharacter
        clip: true
        activeFocusOnTab: root.enabled && !root.imeShielded
        cursorVisible: root.inputFocused
        Accessible.name: root.Accessible.name || root.labelText || root.placeholderText
        Accessible.description: root.supportingText || root.Accessible.description
        cursorDelegate: DTextCursor {
            id: fieldCursor

            color: root.outlined ? (root.isError ? Style.error : Style.primary) : textInput.color
            x: textInput.cursorRectangle.x
            y: textInput.cursorRectangle.y
            height: textInput.cursorRectangle.height
            shown: root.inputFocused

            readonly property int inputCursorPosition: textInput.cursorPosition
            readonly property string inputText: textInput.text

            onInputCursorPositionChanged: resetBlink()
            onInputTextChanged: resetBlink()
        }
        KeyNavigation.tab: root.keyNavigationTab
        KeyNavigation.backtab: root.keyNavigationBacktab
        onTextChanged: root.textEdited()
        onEditingFinished: root.editingFinished()
        onAccepted: root.accepted()
        onActiveFocusChanged: {
            if (activeFocus && root.imeShielded)
                imeSink.forceActiveFocus();
        }
        Keys.forwardTo: root.keyForwardTargets
        Keys.onLeftPressed: event => {
            event.accepted = root.ignoreLeftRightKeys;
        }
        Keys.onRightPressed: event => {
            event.accepted = root.ignoreLeftRightKeys;
        }
        Keys.onPressed: event => {
            if (root.ignoreTabKeys && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
                event.accepted = false;
                for (var i = 0; i < root.keyForwardTargets.length; i++) {
                    if (root.keyForwardTargets[i])
                        root.keyForwardTargets[i].Keys.pressed(event);
                }
                return;
            }
            if (root.ignoreUpDownKeys && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                event.accepted = false;
                for (var i = 0; i < root.keyForwardTargets.length; i++) {
                    if (root.keyForwardTargets[i])
                        root.keyForwardTargets[i].Keys.pressed(event);
                }
                return;
            }
            if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) && root.keyForwardTargets.length > 0) {
                for (var i = 0; i < root.keyForwardTargets.length; i++) {
                    if (root.keyForwardTargets[i])
                        root.keyForwardTargets[i].Keys.pressed(event);
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.IBeamCursor
            acceptedButtons: Qt.NoButton
        }
    }

    Row {
        id: rightButtonsRow

        anchors.right: parent.right
        anchors.rightMargin: (root.outlined || root.expressive ? Style.spacingXS : Style.spacingS) + root.rightAccessoryWidth
        anchors.verticalCenter: textInput.verticalCenter
        spacing: root.outlined || root.expressive ? 0 : Style.spacingXS
        visible: showPasswordToggle || (showClearButton && !readOnly && text.length > 0) || trailingContent !== null || showAcceptButton || (busy && expressive && !morph)

        Loader {
            active: root.showPasswordToggle
            visible: active && (!root.expressive || root.text.length > 0 || root.passwordVisible)
            sourceComponent: DActionButton {
                checkable: true
                checked: root.passwordVisible
                Accessible.name: root.passwordVisible ? I18n.tr("Hide password") : I18n.tr("Show password")
                buttonSize: root.accessorySize
                iconName: root.passwordVisible ? "visibility_off" : "visibility"
                iconSize: root.outlined ? Style.iconSize : Style.iconSizeSmall
                iconColor: root.outlined && root.isError ? (fieldHover.hovered && !root.inputFocused ? Style.onErrorContainer : Style.error) : Style.onSurfaceVariant
                onClicked: root.passwordVisible = !root.passwordVisible
            }
        }

        Loader {
            active: root.showClearButton && !root.readOnly
            visible: active && root.text.length > 0
            sourceComponent: DActionButton {
                objectName: "clearButton"
                Accessible.name: I18n.tr("Clear")
                buttonSize: root.accessorySize
                iconName: "close"
                iconSize: root.outlined ? Style.iconSize : Style.iconSizeSmall
                iconColor: root.outlined && root.isError ? (fieldHover.hovered && !root.inputFocused ? Style.onErrorContainer : Style.error) : Style.onSurfaceVariant
                onClicked: {
                    root.cleared();
                    root.clear();
                }
            }
        }

        Loader {
            active: root.trailingContent !== null
            visible: active
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: root.trailingContent
        }

        Loader {
            active: root.busy && root.expressive && !root.morph
            visible: active
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: Item {
                width: root.accessorySize
                height: root.accessorySize

                DSpinner {
                    anchors.centerIn: parent
                    size: Style.iconSize
                    running: root.busy && root.visible
                }
            }
        }

        Loader {
            active: root.showAcceptButton
            visible: active && !root.busy
            sourceComponent: DActionButton {
                Accessible.name: root.acceptButtonName
                buttonSize: root.accessorySize
                iconName: "keyboard_return"
                iconSize: root.outlined ? Style.iconSize : Style.iconSizeSmall
                iconColor: Style.onSurfaceVariant
                enabled: root.enabled
                onClicked: root.accepted()
            }
        }
    }

    Item {
        anchors.fill: textInput
        visible: textInput.text.length === 0 && !root.composing && opacity > 0
        opacity: root.outlined ? Math.max(0, Math.min(1, placeholderMotion.value)) : root.placeholderVisible ? 1 : 0

        StyledText {
            anchors.fill: parent
            text: root.shownPlaceholderText
            font: textInput.font
            color: root.outlined && !root.enabled ? Style.onSurface_38 : placeholderColor
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: textInput.verticalAlignment
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: I18n.isRtl ? Text.ElideLeft : Text.ElideRight
        }
    }

    StyledText {
        id: supportingLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.contentPadding
        anchors.rightMargin: root.contentPadding
        visible: (root.outlined || root.expressive) && root.shownSupportingText !== ""
        text: root.shownSupportingText
        font.pixelSize: Style.fontSizeSmall
        lineHeightMode: Text.FixedHeight
        lineHeight: Style.outlinedFieldLabelLineHeight
        color: !root.enabled ? Style.onSurface_38 : root.isError ? Style.error : Style.onSurfaceVariant
        wrapMode: Text.WordWrap
    }

    SpringMotion {
        id: placeholderMotion
        enabled: root.outlined
        stiffness: Style.springPreset(root.placeholderVisible ? "slowEffects" : "fastEffects").stiffness
        damping: Style.springPreset(root.placeholderVisible ? "slowEffects" : "fastEffects").damping
    }

    onPlaceholderVisibleChanged: placeholderMotion.retarget(placeholderVisible ? 1 : 0)

    Loader {
        id: shakeLoader
        active: root.expressive
        sourceComponent: SequentialAnimation {
            NumberAnimation {
                target: root
                property: "shakeOffset"
                to: Style.spacingS
                duration: root.spatialDuration / 3
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
            }
            NumberAnimation {
                target: root
                property: "shakeOffset"
                to: -Style.spacingS
                duration: root.spatialDuration / 3
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
            }
            NumberAnimation {
                target: root
                property: "shakeOffset"
                to: 0
                duration: root.spatialDuration / 3
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
            }
        }
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
