// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

OverlayDialog {
    id: root

    property string currentHotkey: ""
    property bool allowSingleKey: true
    property bool singleKeyOnly: false
    property var heldModifiers: []

    signal captured(string hotkey)

    maximumWidth: 420
    acceptEnabled: false

    function openFor(dialogTitle, hotkeyValue, allowSingle, singleOnly) {
        title = dialogTitle || "";
        currentHotkey = hotkeyValue || "";
        if (allowSingle !== undefined)
            allowSingleKey = allowSingle;
        if (singleOnly !== undefined)
            singleKeyOnly = singleOnly;
        heldModifiers = [];
        open();
        Qt.callLater(() => keyTrap.forceActiveFocus());
    }

    function isModifierKey(key) {
        return key === Qt.Key_Control
                || key === Qt.Key_Shift
                || key === Qt.Key_Alt
                || key === Qt.Key_AltGr
                || key === Qt.Key_Meta
                || key === Qt.Key_Super_L
                || key === Qt.Key_Super_R
                || key === Qt.Key_Hyper_L
                || key === Qt.Key_Hyper_R
                || key === Qt.Key_CapsLock;
    }

    function updateHeldModifiers(mods, pressedKey, isRelease) {
        if (singleKeyOnly) {
            heldModifiers = [];
            return;
        }
        let ctrl = (mods & Qt.ControlModifier) !== 0;
        let alt = (mods & Qt.AltModifier) !== 0;
        let meta = (mods & Qt.MetaModifier) !== 0;
        let shift = (mods & Qt.ShiftModifier) !== 0;

        if (pressedKey === Qt.Key_Control)
            ctrl = !isRelease;
        else if (pressedKey === Qt.Key_Alt || pressedKey === Qt.Key_AltGr)
            alt = !isRelease;
        else if (pressedKey === Qt.Key_Meta || pressedKey === Qt.Key_Super_L || pressedKey === Qt.Key_Super_R || pressedKey === Qt.Key_Hyper_L || pressedKey === Qt.Key_Hyper_R)
            meta = !isRelease;
        else if (pressedKey === Qt.Key_Shift)
            shift = !isRelease;

        const out = [];
        if (ctrl)
            out.push("Ctrl");
        if (alt)
            out.push("Alt");
        if (meta)
            out.push("Super");
        if (shift)
            out.push("Shift");
        heldModifiers = out;
    }

    onOpenedChanged: {
        if (opened) {
            heldModifiers = [];
            Qt.callLater(() => keyTrap.forceActiveFocus());
        }
    }

    FocusScope {
        id: keyTrap
        width: parent.width
        implicitHeight: captureCol.implicitHeight
        focus: true

        Keys.onPressed: event => {
            if (!root.opened || event.isAutoRepeat) {
                event.accepted = true;
                return;
            }
            if (root.isModifierKey(event.key)) {
                root.updateHeldModifiers(event.modifiers, event.key, false);
                event.accepted = true;
                return;
            }
            if (event.key === Qt.Key_Escape) {
                root.close();
                event.accepted = true;
                return;
            }
            const relevantMods = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier);
            if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && (root.singleKeyOnly || relevantMods === 0)) {
                root.captured("");
                root.close();
                event.accepted = true;
                return;
            }
            const formatted = Backend.formatKeyEvent(event.key, event.modifiers, event.text, root.allowSingleKey, root.singleKeyOnly);
            if (formatted && formatted !== "") {
                root.captured(formatted);
                root.close();
                event.accepted = true;
            }
        }

        Keys.onReleased: event => {
            if (root.opened && root.isModifierKey(event.key)) {
                root.updateHeldModifiers(event.modifiers, event.key, true);
                event.accepted = true;
            }
        }

        ColumnLayout {
            id: captureCol
            width: parent.width
            spacing: Style.spacingM

            DIcon {
                Layout.alignment: Qt.AlignHCenter
                name: "keyboard"
                size: 44
                color: Style.primary
            }

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                text: root.singleKeyOnly ? I18n.tr("Press a single key…") : I18n.tr("Press shortcut…")
                font.pixelSize: Style.fontSizeLarge
                font.weight: Style.fontWeightBold
                color: Style.surfaceText
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Style.spacingXS

                readonly property var previewParts: {
                    if (root.heldModifiers.length > 0)
                        return root.heldModifiers;
                    if (root.currentHotkey !== "" && typeof Backend !== "undefined")
                        return Backend.prettyHotkeyParts(root.currentHotkey) || [root.currentHotkey];
                    return [];
                }

                Repeater {
                    model: parent.previewParts
                    DKeycap {
                        required property var modelData
                        text: String(modelData)
                    }
                }

                StyledText {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.heldModifiers.length > 0
                    text: "…"
                    font.pixelSize: Style.fontSizeMedium
                    color: Style.surfaceVariantText
                }

                StyledText {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: parent.previewParts.length === 0
                    text: I18n.tr("Disabled")
                    font.pixelSize: Style.fontSizeMedium
                    color: Style.surfaceVariantText
                }
            }

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                text: I18n.tr("Escape to cancel · Backspace to disable")
                font.pixelSize: Style.fontSizeSmall
                color: Style.surfaceVariantText
            }
        }
    }

    leftActions: [
        DButton {
            text: I18n.tr("Disable")
            iconName: "delete"
            variant: "tonal"
            danger: true
            onClicked: {
                root.captured("");
                root.close();
            }
        }
    ]

    actions: [
        DButton {
            text: I18n.tr("Cancel")
            variant: "text"
            onClicked: root.close()
        }
    ]
}
