// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets

Rectangle {
    id: root

    property int value: 0
    property int minValue: 0
    property int maxValue: 1000
    property int step: 1
    property string unit: "ms"
    property string zeroLabel: ""

    signal valueEdited(int newValue)

    readonly property bool usingZeroLabel: value === 0 && zeroLabel.length > 0

    function commitInput() {
        const parsed = parseInt(valueInput.text.trim(), 10);
        if (isNaN(parsed)) {
            valueInput.text = String(root.value);
            return;
        }
        const clamped = Math.max(root.minValue, Math.min(root.maxValue, parsed));
        valueInput.text = String(clamped);
        if (clamped !== root.value)
            root.valueEdited(clamped);
    }

    function stepBy(delta) {
        let base = root.value;
        if (valueInput.activeFocus) {
            const parsed = parseInt(valueInput.text.trim(), 10);
            if (!isNaN(parsed))
                base = Math.max(root.minValue, Math.min(root.maxValue, parsed));
            valueInput.focus = false;
        }
        const next = Math.max(root.minValue, Math.min(root.maxValue, base + delta));
        valueInput.text = String(next);
        if (next !== root.value)
            root.valueEdited(next);
    }

    onValueChanged: {
        if (!valueInput.activeFocus)
            valueInput.text = String(value);
    }

    implicitWidth: stepperRow.implicitWidth + Style.spacingXS * 2
    implicitHeight: 34
    radius: Style.cornerRadiusS
    color: Style.surfaceContainerHigh
    border.width: 1
    border.color: valueInput.activeFocus ? Style.primary : Style.withAlpha(Style.outlineVariant, 0.55)

    RowLayout {
        id: stepperRow
        anchors.fill: parent
        anchors.leftMargin: Style.spacingXS
        anchors.rightMargin: Style.spacingXS
        spacing: Style.spacingXXS

        DActionButton {
            Layout.alignment: Qt.AlignVCenter
            iconName: "remove"
            iconSize: 16
            buttonSize: 26
            circular: false
            enabled: root.value > root.minValue
            tooltipText: I18n.tr("Decrease")
            onClicked: root.stepBy(-root.step)
        }

        Item {
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: 68
            Layout.preferredWidth: Math.max(68, valueInputRow.implicitWidth + Style.spacingXS * 2)
            Layout.fillHeight: true

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                onClicked: {
                    valueInput.text = String(root.value);
                    valueInput.forceActiveFocus();
                    valueInput.selectAll();
                }
            }

            StyledText {
                anchors.centerIn: parent
                visible: root.usingZeroLabel && !valueInput.activeFocus
                text: root.zeroLabel
                font.pixelSize: Style.fontSizeSmall
                font.weight: Style.fontWeightMedium
                color: Style.onSurfaceVariant
            }

            Row {
                id: valueInputRow
                anchors.centerIn: parent
                visible: !root.usingZeroLabel || valueInput.activeFocus
                spacing: 3

                TextInput {
                    id: valueInput
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(24, contentWidth + 4)
                    text: String(root.value)
                    color: Style.surfaceText
                    selectionColor: Style.withAlpha(Style.primary, 0.3)
                    selectedTextColor: Style.surfaceText
                    font.family: Style.monoFontFamily
                    font.pixelSize: Style.fontSizeSmall
                    font.weight: Style.fontWeightMedium
                    horizontalAlignment: Text.AlignHCenter
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator {
                        bottom: root.minValue
                        top: root.maxValue
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            text = String(root.value);
                            selectAll();
                        } else {
                            root.commitInput();
                        }
                    }
                    Keys.onReturnPressed: {
                        root.commitInput();
                        focus = false;
                    }
                    Keys.onEnterPressed: {
                        root.commitInput();
                        focus = false;
                    }
                    Keys.onEscapePressed: {
                        text = String(root.value);
                        focus = false;
                    }
                }

                StyledText {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.unit.length > 0
                    text: root.unit
                    font.family: Style.monoFontFamily
                    font.pixelSize: Style.fontSizeSmall
                    color: Style.onSurfaceVariant
                }
            }
        }

        DActionButton {
            Layout.alignment: Qt.AlignVCenter
            iconName: "add"
            iconSize: 16
            buttonSize: 26
            circular: false
            enabled: root.value < root.maxValue
            tooltipText: I18n.tr("Increase")
            onClicked: root.stepBy(root.step)
        }
    }
}
