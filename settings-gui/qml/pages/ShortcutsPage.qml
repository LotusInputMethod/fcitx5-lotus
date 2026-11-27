// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets
import "../components"
import "../components/ListModelSync.js" as ListModelSync

SettingsPage {
    id: root

    function strVal(key, fallback) {
        const v = Backend.values[key];
        if (v === undefined || v === null)
            return fallback;
        if (typeof v === "object") {
            if (v["0"] !== undefined)
                return String(v["0"] ?? "");
            if (Array.isArray(v) && v.length > 0)
                return String(v[0] ?? "");
            return fallback;
        }
        return String(v);
    }

    ListModel {
        id: modeListModel
    }

    function syncModeListModel() {
        ListModelSync.syncByKey(
            modeListModel,
            typeof Backend !== "undefined" ? (Backend.modeList || []) : [],
            m => String(m.name ?? ""),
            m => ({
                modeName: String(m.name ?? ""),
                modeTitle: String(m.title ?? m.name ?? ""),
                modeShortcut: String(m.shortcut ?? ""),
                modeVisible: Boolean(m.visible),
                hasConflict: Boolean(m.conflict)
            })
        );
    }

    Connections {
        target: typeof Backend !== "undefined" ? Backend : null
        ignoreUnknownSignals: true
        function onModeListChanged() { root.syncModeListModel(); }
        function onStateChanged() { root.syncModeListModel(); }
    }

    Component.onCompleted: syncModeListModel()

    function applyPermutation(indices) {
        if (!Array.isArray(indices) || indices.length < 2)
            return;
        let first = -1;
        let last = -1;
        for (let k = 0; k < indices.length; k++) {
            if (indices[k] !== k) {
                if (first === -1)
                    first = k;
                last = k;
            }
        }
        if (first < 0 || last <= first)
            return;
        if (indices[first] === first + 1 && indices[last] === first)
            Backend.reorderModes(first, last);
        else
            Backend.reorderModes(last, first);
    }

    // Main Shortcuts
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Main Shortcuts")
            sectionId: "main_shortcuts"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: Backend.optionTitle("ModeMenuKey")
                    showSeparator: true

                    HotkeyButton {
                        hotkey: root.strVal("ModeMenuKey", String(Backend.optionDefault("ModeMenuKey")))
                        allowSingleKey: true
                        singleKeyOnly: false
                        dialogTitle: I18n.tr("Mode Menu Hotkey")
                        onHotkeyCaptured: k => Backend.setValue("ModeMenuKey", k)
                    }
                }

                SettingRow {
                    title: Backend.optionTitle("CycleModeKey")

                    HotkeyButton {
                        hotkey: root.strVal("CycleModeKey", String(Backend.optionDefault("CycleModeKey")))
                        allowSingleKey: false
                        singleKeyOnly: false
                        dialogTitle: I18n.tr("Cycle Mode Hotkey")
                        onHotkeyCaptured: k => Backend.setValue("CycleModeKey", k)
                    }
                }
            }
        }
    }

    // Mode Menu
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Mode Menu")
            sectionId: "mode_menu"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            DReorderList {
                id: modeList
                width: parent.width
                spacing: Style.spacingXXS
                flickable: root
                model: modeListModel
                onReordered: indices => root.applyPermutation(indices)

                delegate: Item {
                    id: modeRowWrap
                    required property string modeName
                    required property string modeTitle
                    required property string modeShortcut
                    required property bool modeVisible
                    required property bool hasConflict
                    required property int index

                    readonly property bool isDragging: modeList.draggingIndex === index

                    function focusHandle(reason) {
                        dragHandle.forceActiveFocus(reason);
                    }

                    width: modeList.width
                    implicitHeight: modeRow.implicitHeight
                    height: implicitHeight
                    z: isDragging ? 10 : 1
                    scale: isDragging ? 1.01 : 1.0

                    Behavior on scale {
                        enabled: !Style.reduceMotion
                        DAnim {
                            duration: Style.expressiveDurations.expressiveFastSpatial
                            easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
                        }
                    }

                    Behavior on y {
                        enabled: !modeRowWrap.isDragging && modeList.animateLayout && !Style.reduceMotion
                        DAnim {
                            duration: Style.expressiveDurations.expressiveFastSpatial
                            easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
                        }
                    }

                    Rectangle {
                        id: modeRow
                        anchors.fill: parent
                        implicitHeight: Math.max(54, rowInner.implicitHeight + Style.spacingS * 2)
                        radius: Style.cornerRadiusS
                        color: {
                            if (modeRowWrap.hasConflict)
                                return Style.withAlpha(Style.errorContainer, 0.45);
                            if (modeRowWrap.isDragging)
                                return Style.surfaceContainerHigh;
                            if (rowHover.hovered)
                                return Style.withAlpha(Style.surfaceText, 0.04);
                            return "transparent";
                        }
                        border.width: modeRowWrap.hasConflict ? Style.outlineWidth : (modeRowWrap.isDragging ? Style.outlineWidth : 0)
                        border.color: modeRowWrap.hasConflict ? Style.error : (modeRowWrap.isDragging ? Style.primary : "transparent")
                        opacity: modeRowWrap.modeVisible ? 1.0 : 0.65

                        Behavior on color {
                            enabled: !Style.reduceMotion
                            DColorAnim {
                                duration: Style.expressiveDurations.expressiveFastEffects
                                easing.bezierCurve: Style.expressiveCurves.expressiveFastEffects
                            }
                        }

                        HoverHandler {
                            id: rowHover
                        }

                        RowLayout {
                            id: rowInner
                            anchors.fill: parent
                            anchors.leftMargin: Style.spacingS
                            anchors.rightMargin: Style.spacingL
                            anchors.topMargin: Style.spacingS
                            anchors.bottomMargin: Style.spacingS
                            spacing: Style.spacingS

                            DDragHandle {
                                id: dragHandle
                                coordinateItem: modeList
                                label: modeRowWrap.modeTitle
                                dragging: modeRowWrap.isDragging
                                canMoveUp: modeRowWrap.index > 0
                                canMoveDown: modeRowWrap.index < modeList.count - 1
                                onStarted: pos => modeList.begin(modeRowWrap.index, pos)
                                onMoved: pos => modeList.dragTo(pos)
                                onFinished: modeList.finish()
                                onDragCanceled: modeList.cancel()
                                onMoveRequested: delta => modeList.move(modeRowWrap.index, delta)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: Style.spacingXXS

                                StyledText {
                                    Layout.fillWidth: true
                                    text: modeRowWrap.modeTitle
                                    font.pixelSize: Style.fontSizeMedium
                                    font.weight: Style.fontWeightMedium
                                    color: modeRowWrap.hasConflict ? Style.error : Style.surfaceText
                                    elide: Text.ElideRight
                                }

                                StyledText {
                                    Layout.fillWidth: true
                                    visible: modeRowWrap.hasConflict
                                    text: I18n.tr("Duplicate key '{}'").replace("{}", modeRowWrap.modeShortcut)
                                    font.pixelSize: Style.fontSizeSmall
                                    color: Style.error
                                    elide: Text.ElideRight
                                }
                            }

                            Item {
                                visible: modeRowWrap.hasConflict
                                implicitWidth: Style.iconSizeMedium
                                implicitHeight: Style.iconSizeMedium
                                Layout.alignment: Qt.AlignVCenter

                                DIcon {
                                    anchors.centerIn: parent
                                    name: "warning"
                                    size: Style.iconSizeSmall
                                    color: Style.error
                                }

                                MouseArea {
                                    id: conflictHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }

                                DTooltipHost {
                                    text: modeRowWrap.hasConflict
                                          ? I18n.tr("Duplicate shortcut '{}' used for multiple enabled modes.").replace("{}", modeRowWrap.modeShortcut)
                                          : null
                                    multiline: true
                                    target: parent
                                    hoverArea: conflictHover
                                    side: "top"
                                }
                            }

                            HotkeyButton {
                                Layout.alignment: Qt.AlignVCenter
                                enabled: modeRowWrap.modeVisible
                                hotkey: modeRowWrap.modeShortcut
                                conflict: modeRowWrap.hasConflict
                                allowSingleKey: true
                                singleKeyOnly: true
                                dialogTitle: modeRowWrap.modeTitle
                                onHotkeyCaptured: k => Backend.setModeShortcut(modeRowWrap.modeName, k)
                            }

                            DToggle {
                                Layout.alignment: Qt.AlignVCenter
                                hideText: true
                                checked: modeRowWrap.modeVisible
                                onToggled: vis => Backend.setModeVisible(modeRowWrap.modeName, vis)
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: Style.spacingL
                            anchors.rightMargin: Style.spacingL
                            height: 1
                            color: Style.withAlpha(Style.outlineVariant, 0.45)
                            visible: modeRowWrap.index < modeList.count - 1 && !modeRowWrap.isDragging
                        }
                    }
                }
            }
        }
    }
}
