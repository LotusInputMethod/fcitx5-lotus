// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import "../DCommon/Widgets" as DW
import "../DCommon/Common" as DC
import "../components" as LC
import "../components/ListModelSync.js" as ListModelSync

Item {
    id: pageRoot

    property string searchQuery: ""
    property string expandedAppId: ""

    readonly property var modeOptions: Backend.getAppModeOptions()
    readonly property var modeLabels: modeOptions.map(function(option) { return String(option.label) })
    readonly property var modeCodes: modeOptions.map(function(option) { return String(option.value) })

    readonly property var filteredAppRules: {
        var rules = Backend.appRules || []
        var q = searchQuery.trim().toLowerCase()
        if (!q)
            return rules
        var out = []
        for (var i = 0; i < rules.length; i++) {
            var r = rules[i]
            var appStr = String(r.app || "").toLowerCase()
            var nameStr = String(r.displayName || "").toLowerCase()
            if (appStr.indexOf(q) !== -1 || nameStr.indexOf(q) !== -1)
                out.push(r)
        }
        return out
    }

    ListModel {
        id: filteredRulesModel
    }

    function toRuleModelItem(r) {
        return {
            app: String(r.app || ""),
            displayName: String(r.displayName || r.app || ""),
            iconName: String(r.iconName || ""),
            mode: r.mode !== undefined ? Number(r.mode) : 1,
            commit: r.commit !== undefined ? Number(r.commit) : 0,
            backspace: r.backspace !== undefined ? Number(r.backspace) : 0,
            post: r.post !== undefined ? Number(r.post) : 0
        }
    }

    function syncFilteredRulesModel() {
        ListModelSync.syncByKey(
            filteredRulesModel,
            filteredAppRules,
            function(r) { return String(r.app || "") },
            toRuleModelItem
        )
    }

    onFilteredAppRulesChanged: syncFilteredRulesModel()
    Component.onCompleted: syncFilteredRulesModel()

    function intSetting(key, fallback) {
        if (!Backend.values || Backend.values[key] === undefined)
            return fallback
        var val = parseInt(Backend.values[key], 10)
        return isNaN(val) ? fallback : val
    }

    function modeLabel(modeCode) {
        var idx = modeCodes.indexOf(String(modeCode))
        if (idx >= 0)
            return modeLabels[idx]
        return I18n.tr("OFF")
    }

    function iconUrlFor(iconName) {
        if (!iconName)
            return ""
        if (iconName.indexOf("/") === 0)
            return "file://" + iconName
        return "image://theme/" + iconName
    }

    function avatarLetter(displayName, appId) {
        var src = String(displayName || appId || "?").trim()
        return src.length > 0 ? src.charAt(0).toUpperCase() : "?"
    }

    function ruleSubtitle(rule) {
        var parts = []
        var displayName = String(rule.displayName || "")
        var appId = String(rule.app || "")
        if (displayName && displayName.toLowerCase() !== appId.toLowerCase())
            parts.push(appId)
        if ((rule.commit || 0) > 0 || (rule.backspace || 0) > 0 || (rule.post || 0) > 0)
            parts.push(I18n.tr("Custom Timing"))
        return parts.join(" · ")
    }

    function hasRule(appId) {
        var rules = Backend.appRules || []
        for (var i = 0; i < rules.length; i++) {
            if (String(rules[i].app || "") === appId)
                return true
        }
        return false
    }

    LC.SettingsPage {
        // Section 1: Default Timing
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Default Timing")
                sectionId: "default_timing"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: timingCol
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: [
                            {
                                key: "DefaultCommitInterval",
                                title: Backend.optionTitle("DefaultCommitInterval"),
                                tooltip: Backend.optionDescription("DefaultCommitInterval")
                            },
                            {
                                key: "DefaultBackspaceInterval",
                                title: Backend.optionTitle("DefaultBackspaceInterval"),
                                tooltip: Backend.optionDescription("DefaultBackspaceInterval")
                            },
                            {
                                key: "DefaultPostDelay",
                                title: Backend.optionTitle("DefaultPostDelay"),
                                tooltip: Backend.optionDescription("DefaultPostDelay")
                            }
                        ]

                        LC.SettingRow {
                            id: timingRow
                            required property var modelData
                            required property int index
                            readonly property int currentVal: pageRoot.intSetting(modelData.key, Number(Backend.optionDefault(modelData.key)))

                            Layout.fillWidth: true
                            title: modelData.title
                            tooltipText: modelData.tooltip
                            showSeparator: index < 2

                            LC.TimingStepper {
                                minValue: 1
                                value: timingRow.currentVal
                                onValueEdited: function(newVal) {
                                    Backend.setValue(timingRow.modelData.key, newVal)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Section 2: Application Rules
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Application Rules")
                sectionId: "app_rules"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: rulesCol
                    width: parent.width
                    spacing: DC.Style.spacingS

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: DC.Style.spacingS

                            DW.DSearchField {
                                id: appSearchField
                                Layout.fillWidth: true
                                placeholderText: I18n.tr("Search applications...")
                                onTextEdited: pageRoot.searchQuery = text
                            }

                            DW.DButton {
                                text: I18n.tr("Add")
                                iconName: "add"
                                tooltipText: I18n.tr("Add Rule")
                                onClicked: addAppDialog.open()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                        }

                        DW.StyledText {
                            Layout.fillWidth: true
                            Layout.topMargin: DC.Style.spacingM
                            Layout.bottomMargin: DC.Style.spacingM
                            horizontalAlignment: Text.AlignHCenter
                            visible: filteredRulesModel.count === 0
                            color: DC.Style.onSurfaceVariant
                            text: (Backend.appRules && Backend.appRules.length > 0 && pageRoot.searchQuery.trim().length > 0)
                                ? I18n.tr("No Matching Apps")
                                : I18n.tr("No Application Rules")
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            visible: filteredRulesModel.count > 0

                            Repeater {
                                model: filteredRulesModel

                                ColumnLayout {
                                    id: ruleCard
                                    required property int index
                                    required property string app
                                    required property string displayName
                                    required property string iconName
                                    required property int mode
                                    required property int commit
                                    required property int backspace
                                    required property int post

                                    readonly property string appId: app
                                    readonly property int modeVal: mode
                                    readonly property int commitVal: commit
                                    readonly property int backspaceVal: backspace
                                    readonly property int postVal: post
                                    readonly property bool isExpanded: pageRoot.expandedAppId === appId
                                    readonly property string subtitle: pageRoot.ruleSubtitle({
                                        app: app,
                                        displayName: displayName,
                                        commit: commit,
                                        backspace: backspace,
                                        post: post
                                    })

                                    Layout.fillWidth: true
                                    spacing: 0

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Math.max(DC.Style.listItemHeight, headerRow.implicitHeight + DC.Style.spacingS * 2)
                                        radius: DC.Style.cornerRadiusS
                                        color: headerMouse.containsMouse ? DC.Style.withAlpha(DC.Style.surfaceText, 0.04) : "transparent"

                                        MouseArea {
                                            id: headerMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: pageRoot.expandedAppId = ruleCard.isExpanded ? "" : ruleCard.appId
                                        }

                                        RowLayout {
                                            id: headerRow
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.leftMargin: DC.Style.spacingS
                                            anchors.rightMargin: DC.Style.spacingS
                                            spacing: DC.Style.spacingM

                                            Item {
                                                Layout.preferredWidth: 28
                                                Layout.preferredHeight: 28
                                                Layout.alignment: Qt.AlignVCenter

                                                Image {
                                                    id: rowIcon
                                                    anchors.fill: parent
                                                    sourceSize.width: 28
                                                    sourceSize.height: 28
                                                    fillMode: Image.PreserveAspectFit
                                                    cache: true
                                                    source: pageRoot.iconUrlFor(ruleCard.iconName)
                                                    visible: ruleCard.iconName !== "" && status === Image.Ready
                                                }

                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: width / 2
                                                    color: DC.Style.secondaryContainer
                                                    visible: !rowIcon.visible

                                                    DW.StyledText {
                                                        anchors.centerIn: parent
                                                        text: pageRoot.avatarLetter(ruleCard.displayName, ruleCard.appId)
                                                        font.pixelSize: DC.Style.fontSizeSmall
                                                        font.weight: DC.Style.fontWeightBold
                                                        color: DC.Style.onSecondaryContainer
                                                    }
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2

                                                DW.StyledText {
                                                    Layout.fillWidth: true
                                                    text: ruleCard.displayName
                                                    font.pixelSize: DC.Style.fontSizeMedium
                                                    font.weight: DC.Style.fontWeightMedium
                                                    color: DC.Style.surfaceText
                                                    maximumLineCount: 1
                                                    wrapMode: Text.NoWrap
                                                    elide: Text.ElideRight
                                                }

                                                DW.StyledText {
                                                    Layout.fillWidth: true
                                                    visible: ruleCard.subtitle !== ""
                                                    text: ruleCard.subtitle
                                                    font.pixelSize: DC.Style.fontSizeSmall
                                                    color: DC.Style.onSurfaceVariant
                                                    maximumLineCount: 1
                                                    wrapMode: Text.NoWrap
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Rectangle {
                                                Layout.alignment: Qt.AlignVCenter
                                                implicitWidth: modePillText.implicitWidth + DC.Style.spacingM
                                                implicitHeight: DC.Style.buttonHeightXXS
                                                radius: height / 2
                                                color: DC.Style.secondaryContainer

                                                DW.StyledText {
                                                    id: modePillText
                                                    anchors.centerIn: parent
                                                    text: pageRoot.modeLabel(ruleCard.modeVal)
                                                    font.pixelSize: DC.Style.fontSizeSmall
                                                    font.weight: DC.Style.fontWeightMedium
                                                    color: DC.Style.onSecondaryContainer
                                                }
                                            }

                                            DW.DIcon {
                                                Layout.alignment: Qt.AlignVCenter
                                                name: "expand_more"
                                                size: DC.Style.iconSize
                                                color: DC.Style.onSurfaceVariant
                                                rotation: ruleCard.isExpanded ? 180 : 0
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: DC.Style.spacingS
                                        Layout.rightMargin: DC.Style.spacingS
                                        Layout.topMargin: DC.Style.spacingXS
                                        Layout.bottomMargin: DC.Style.spacingS
                                        visible: ruleCard.isExpanded
                                        implicitHeight: expandedCol.implicitHeight + DC.Style.spacingM * 2
                                        radius: DC.Style.cornerRadiusS
                                        color: DC.Style.surfaceContainerLow

                                        ColumnLayout {
                                            id: expandedCol
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.margins: DC.Style.spacingM
                                            spacing: DC.Style.spacingM

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: DC.Style.spacingM

                                                DW.StyledText {
                                                    Layout.fillWidth: true
                                                    text: I18n.tr("Input Mode")
                                                    font.pixelSize: DC.Style.fontSizeMedium
                                                    color: DC.Style.surfaceText
                                                }

                                                DW.DDropdown {
                                                    id: modeDropdown
                                                    dropdownWidth: 210
                                                    Layout.preferredWidth: 210
                                                    options: pageRoot.modeLabels
                                                    optionValues: pageRoot.modeCodes
                                                    onValueChanged: function(val) {
                                                        Backend.updateAppRule(
                                                            ruleCard.appId,
                                                            parseInt(val, 10),
                                                            ruleCard.commitVal,
                                                            ruleCard.backspaceVal,
                                                            ruleCard.postVal
                                                        )
                                                    }

                                                    Binding {
                                                        target: modeDropdown
                                                        property: "currentValue"
                                                        value: String(ruleCard.modeVal)
                                                    }
                                                }
                                            }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: DC.Style.spacingS

                                                    DW.StyledText {
                                                        Layout.fillWidth: true
                                                        text: I18n.tr("Timing Overrides (0 = inherit default)")
                                                        font.pixelSize: DC.Style.fontSizeSmall
                                                        color: DC.Style.onSurfaceVariant
                                                    }

                                                    Repeater {
                                                        model: [
                                                            { field: "commit", label: I18n.tr("Commit Interval") },
                                                            { field: "backspace", label: I18n.tr("Backspace Interval") },
                                                            { field: "post", label: I18n.tr("Post Delay") }
                                                        ]

                                                        RowLayout {
                                                            required property var modelData
                                                            Layout.fillWidth: true
                                                            spacing: DC.Style.spacingM

                                                            DW.StyledText {
                                                                Layout.fillWidth: true
                                                                text: modelData.label
                                                                font.pixelSize: DC.Style.fontSizeMedium
                                                                color: DC.Style.surfaceText
                                                            }

                                                            LC.TimingStepper {
                                                                value: modelData.field === "commit"
                                                                    ? ruleCard.commitVal
                                                                    : (modelData.field === "backspace" ? ruleCard.backspaceVal : ruleCard.postVal)
                                                                zeroLabel: I18n.tr("Default")
                                                                onValueEdited: function(newVal) {
                                                                    Backend.updateAppRule(
                                                                        ruleCard.appId,
                                                                        ruleCard.modeVal,
                                                                        modelData.field === "commit" ? newVal : ruleCard.commitVal,
                                                                        modelData.field === "backspace" ? newVal : ruleCard.backspaceVal,
                                                                        modelData.field === "post" ? newVal : ruleCard.postVal
                                                                    )
                                                                }
                                                            }
                                                        }
                                                    }
                                                }

                                            RowLayout {
                                                Layout.fillWidth: true

                                                DW.DButton {
                                                    text: I18n.tr("Remove Rule")
                                                    iconName: "delete"
                                                    variant: "tonal"
                                                    danger: true
                                                    tooltipText: I18n.tr("Remove application rule")
                                                    onClicked: {
                                                        if (pageRoot.expandedAppId === ruleCard.appId)
                                                            pageRoot.expandedAppId = ""
                                                        Backend.removeAppRule(ruleCard.appId)
                                                    }
                                                }

                                                Item {
                                                    Layout.fillWidth: true
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 2
                                        Layout.preferredHeight: 1
                                        color: DC.Style.withAlpha(DC.Style.outlineVariant, 0.45)
                                        visible: ruleCard.index < filteredRulesModel.count - 1
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    LC.OverlayDialog {
        id: addAppDialog

        maximumWidth: 420
        title: I18n.tr("Add Application")
        supportingText: I18n.tr("Enter binary name or window class (e.g. discord, code):")
        acceptEnabled: canAdd

        readonly property string trimmedApp: appNameField.text.trim()
        readonly property bool alreadyExists: trimmedApp.length > 0 && pageRoot.hasRule(trimmedApp)
        readonly property bool canAdd: trimmedApp.length > 0 && !alreadyExists

        function open() {
            appNameField.text = ""
            opened = true
            appNameField.forceActiveFocus()
        }

        function submit() {
            if (!canAdd)
                return
            var val = trimmedApp
            Backend.addAppRule(val)
            pageRoot.expandedAppId = val
            close()
        }

        onAccepted: submit()

        DW.DTextField {
            id: appNameField
            width: parent.width
            outlined: true
            placeholderText: I18n.tr("e.g. app-name")
            isError: addAppDialog.alreadyExists
            supportingText: addAppDialog.alreadyExists ? I18n.tr("Rule already exists") : ""
            onAccepted: addAppDialog.submit()
        }

        actions: [
            DW.DButton {
                text: I18n.tr("Cancel")
                variant: "text"
                onClicked: addAppDialog.close()
            },
            DW.DButton {
                text: I18n.tr("Add")
                enabled: addAppDialog.canAdd
                onClicked: addAppDialog.submit()
            }
        ]
    }
}
