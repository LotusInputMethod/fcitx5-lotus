// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DCommon.Common as DC
import DCommon.Widgets
import "components"

ApplicationWindow {
    id: mainWindow

    // Bind C++ context singletons into DCommon before child pages construct
    property bool _bridgesReady: {
        DC.I18n.backend = typeof I18n !== "undefined" ? I18n : null;
        DC.Style.theme = typeof Theme !== "undefined" ? Theme : null;
        return true;
    }

    property int currentIndex: 0
    property bool _forceClose: false

    readonly property var pagesModel: [
        { id: "general", title: I18n.tr("General"), icon: "preferences-system-symbolic", source: "pages/GeneralPage.qml" },
        { id: "typing", title: I18n.tr("Typing"), icon: "accessories-character-map-symbolic", source: "pages/TypingPage.qml" },
        { id: "shortcuts", title: I18n.tr("Shortcuts"), icon: "preferences-desktop-keyboard-shortcuts-symbolic", source: "pages/ShortcutsPage.qml" },
        { id: "applications", title: I18n.tr("Applications"), icon: "applications-system-symbolic", source: "pages/ApplicationsPage.qml" },
        { id: "macros", title: I18n.tr("Macros"), icon: "insert-text-symbolic", source: "pages/MacrosPage.qml" },
        { id: "dictionary", title: I18n.tr("Dictionary"), icon: "accessories-dictionary-symbolic", source: "pages/DictionaryPage.qml" },
        { id: "keymap", title: I18n.tr("Keymap"), icon: "input-keyboard-symbolic", source: "pages/KeymapPage.qml" },
        { id: "backup", title: I18n.tr("Backup"), icon: "document-save-symbolic", source: "pages/BackupPage.qml" },
        { id: "about", title: I18n.tr("About"), icon: "help-about-symbolic", source: "pages/AboutPage.qml" }
    ]

    readonly property string currentPageTitle: (pagesModel[currentIndex] && pagesModel[currentIndex].title) || ""

    title: I18n.tr("Lotus Settings")
    width: 960
    height: 680
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    color: DC.Style.surface

    onClosing: close => {
        if (_forceClose || typeof Backend === "undefined" || !Backend.hasUnsavedChanges)
            return;
        close.accepted = false;
        closeDialog.openDialog();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar
        Rectangle {
            id: sidebarPane
            Layout.preferredWidth: 220
            Layout.minimumWidth: 220
            Layout.maximumWidth: 220
            Layout.fillHeight: true
            color: DC.Style.surfaceContainerLow

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 1
                color: DC.Style.outlineVariant
                opacity: 0.35
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Sidebar header
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54

                    DragHandler {
                        target: null
                        onActiveChanged: {
                            if (active)
                                mainWindow.startSystemMove();
                        }
                    }

                    TapHandler {
                        onDoubleTapped: {
                            if (mainWindow.visibility === Window.Maximized)
                                mainWindow.showNormal();
                            else
                                mainWindow.showMaximized();
                        }
                    }

                    RowLayout {
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width - DC.Style.spacingM * 2)
                        spacing: DC.Style.spacingS

                        DIcon {
                            name: "fcitx-lotus"
                            size: 20
                            color: DC.Style.primary
                        }

                        StyledText {
                            Layout.fillWidth: true
                            text: I18n.tr("Lotus Settings")
                            font.pixelSize: DC.Style.fontSizeLarge
                            font.weight: DC.Style.fontWeightBold
                            color: DC.Style.surfaceText
                            elide: Text.ElideRight
                        }
                    }
                }

                // Navigation rows
                DFlickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: navColumn.implicitHeight + DC.Style.spacingS * 2
                    clip: true

                    ColumnLayout {
                        id: navColumn
                        x: DC.Style.spacingS
                        y: DC.Style.spacingXS
                        width: parent.width - DC.Style.spacingS * 2
                        spacing: DC.Style.spacingXXS

                        Repeater {
                            model: mainWindow.pagesModel

                            StyledButton {
                                id: navRow
                                required property var modelData
                                required property int index

                                readonly property bool selected: mainWindow.currentIndex === index
                                readonly property color rowContentColor: selected ? DC.Style.onSelectedContainer : DC.Style.surfaceText

                                visible: modelData.id !== "about"
                                Layout.fillWidth: true
                                implicitHeight: Math.max(44, rowInner.implicitHeight + 24)
                                radius: DC.Style.cornerRadiusM
                                color: selected ? DC.Style.selectedContainer : "transparent"
                                Accessible.role: Accessible.ListItem
                                Accessible.name: String(modelData.title ?? "")
                                Accessible.selected: selected
                                onClicked: mainWindow.currentIndex = index

                                StateLayer {
                                    control: navRow
                                    stateColor: navRow.rowContentColor
                                    cornerRadius: navRow.radius
                                }

                                FocusRing {
                                    visible: navRow.visualFocus
                                    radius: navRow.radius + DC.Style.focusRingOffset
                                }

                                RowLayout {
                                    id: rowInner
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 12

                                    DIcon {
                                        name: String(navRow.modelData.icon ?? "")
                                        size: 18
                                        filled: navRow.selected
                                        color: navRow.rowContentColor
                                        Layout.alignment: Qt.AlignVCenter
                                    }

                                    StyledText {
                                        Layout.fillWidth: true
                                        text: String(navRow.modelData.title ?? "")
                                        font.pixelSize: DC.Style.fontSizeMedium
                                        font.weight: navRow.selected ? DC.Style.fontWeightBold : DC.Style.fontWeightMedium
                                        color: navRow.rowContentColor
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                // Bottom About tab (matches sidebar nav row style + version chip)
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: aboutBtn.implicitHeight + DC.Style.spacingS * 2

                    StyledButton {
                        id: aboutBtn
                        readonly property int aboutIndex: mainWindow.pagesModel.length - 1
                        readonly property bool selected: mainWindow.currentIndex === aboutIndex
                        readonly property color rowContentColor: selected ? DC.Style.onSelectedContainer : DC.Style.surfaceText

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: DC.Style.spacingS
                        anchors.rightMargin: DC.Style.spacingS
                        implicitHeight: Math.max(44, aboutRowInner.implicitHeight + 24)
                        radius: DC.Style.cornerRadiusM
                        color: selected ? DC.Style.selectedContainer : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: I18n.tr("About")
                        Accessible.selected: selected
                        onClicked: mainWindow.currentIndex = aboutIndex

                        StateLayer {
                            control: aboutBtn
                            stateColor: aboutBtn.rowContentColor
                            cornerRadius: aboutBtn.radius
                        }

                        FocusRing {
                            visible: aboutBtn.visualFocus
                            radius: aboutBtn.radius + DC.Style.focusRingOffset
                        }

                        RowLayout {
                            id: aboutRowInner
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            DIcon {
                                name: "help-about-symbolic"
                                size: 18
                                filled: aboutBtn.selected
                                color: aboutBtn.rowContentColor
                                Layout.alignment: Qt.AlignVCenter
                            }

                            StyledText {
                                Layout.fillWidth: true
                                text: I18n.tr("About")
                                font.pixelSize: DC.Style.fontSizeMedium
                                font.weight: aboutBtn.selected ? DC.Style.fontWeightBold : DC.Style.fontWeightMedium
                                color: aboutBtn.rowContentColor
                                elide: Text.ElideRight
                            }

                            DBadge {
                                Layout.alignment: Qt.AlignVCenter
                                text: "v" + (typeof Backend !== "undefined" && Backend.appVersion ? Backend.appVersion : "dev")
                            }
                        }
                    }
                }
            }
        }

        // Content pane
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Content header bar
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 54

                DragHandler {
                    target: null
                    onActiveChanged: {
                        if (active)
                            mainWindow.startSystemMove();
                    }
                }

                TapHandler {
                    onDoubleTapped: {
                        if (mainWindow.visibility === Window.Maximized)
                            mainWindow.showNormal();
                        else
                            mainWindow.showMaximized();
                    }
                }

                StyledText {
                    anchors.centerIn: parent
                    text: mainWindow.currentPageTitle
                    font.pixelSize: DC.Style.fontSizeLarge
                    font.weight: DC.Style.fontWeightBold
                    color: DC.Style.surfaceText
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: DC.Style.outlineVariant
                    opacity: 0.25
                }
            }

            OfflineBanner {
                Layout.fillWidth: true
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                StackLayout {
                    anchors.fill: parent
                    currentIndex: mainWindow.currentIndex

                    Repeater {
                        model: mainWindow.pagesModel

                        Loader {
                            required property var modelData
                            required property int index

                            property bool _activatedOnce: index < 3

                            onVisibleChanged: {
                                if (visible)
                                    _activatedOnce = true;
                            }

                            active: _activatedOnce || mainWindow.currentIndex === index
                            source: String(modelData.source ?? "")
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                }

                FloatingActionBar {
                    id: floatingBar
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: toastBanner.active ? 74 : 18
                    z: 20

                    Behavior on anchors.bottomMargin {
                        enabled: !DC.Style.reduceMotion
                        DC.DAnim {
                            duration: DC.Style.expressiveDurations.expressiveEffects
                            easing.bezierCurve: DC.Style.expressiveCurves.expressiveEffects
                        }
                    }
                }

                ToastBanner {
                    id: toastBanner
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    z: 30
                }
            }
        }
    }

    UnsavedCloseDialog {
        id: closeDialog
        onDiscardRequested: {
            if (typeof Backend !== "undefined")
                Backend.cancelAll();
            mainWindow._forceClose = true;
            mainWindow.close();
        }
        onApplyAndCloseRequested: {
            if (typeof Backend !== "undefined") {
                const ok = Backend.applyAll();
                if (ok !== false && !Backend.hasUnsavedChanges) {
                    mainWindow._forceClose = true;
                    mainWindow.close();
                }
            } else {
                mainWindow._forceClose = true;
                mainWindow.close();
            }
        }
    }

    Repeater {
        model: [
            { edges: Qt.LeftEdge, cursor: Qt.SizeHorCursor, sideH: -1, sideV: 0 },
            { edges: Qt.RightEdge, cursor: Qt.SizeHorCursor, sideH: 1, sideV: 0 },
            { edges: Qt.TopEdge, cursor: Qt.SizeVerCursor, sideH: 0, sideV: -1 },
            { edges: Qt.BottomEdge, cursor: Qt.SizeVerCursor, sideH: 0, sideV: 1 },
            { edges: Qt.TopEdge | Qt.LeftEdge, cursor: Qt.SizeFDiagCursor, sideH: -1, sideV: -1 },
            { edges: Qt.TopEdge | Qt.RightEdge, cursor: Qt.SizeBDiagCursor, sideH: 1, sideV: -1 },
            { edges: Qt.BottomEdge | Qt.LeftEdge, cursor: Qt.SizeBDiagCursor, sideH: -1, sideV: 1 },
            { edges: Qt.BottomEdge | Qt.RightEdge, cursor: Qt.SizeFDiagCursor, sideH: 1, sideV: 1 }
        ]

        MouseArea {
            required property var modelData
            readonly property bool isCorner: modelData.sideH !== 0 && modelData.sideV !== 0
            readonly property int thickness: isCorner ? 10 : 5

            x: modelData.sideH === -1 ? 0 : (modelData.sideH === 1 ? mainWindow.width - thickness : 10)
            y: modelData.sideV === -1 ? 0 : (modelData.sideV === 1 ? mainWindow.height - thickness : 10)
            width: modelData.sideH === 0 ? Math.max(0, mainWindow.width - 20) : thickness
            height: modelData.sideV === 0 ? Math.max(0, mainWindow.height - 20) : thickness
            z: 999
            visible: mainWindow.visibility === Window.Windowed
            cursorShape: modelData.cursor
            acceptedButtons: Qt.LeftButton
            onPressed: mainWindow.startSystemResize(modelData.edges)
        }
    }
}
