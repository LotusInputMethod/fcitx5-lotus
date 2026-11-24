// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import "../DCommon/Widgets" as DW
import "../DCommon/Common" as DC
import "../components" as LC
import "../components/ListModelSync.js" as ListModelSync
import "../dialogs" as LD

Item {
    id: pageRoot

    readonly property int pageSize: 180
    property string searchQuery: ""
    property int displayLimit: pageSize

    readonly property int totalWordCount: Backend.totalWordCount

    readonly property var probeWords: {
        var _dep = Backend.dictionaryWords
        return Backend.filterWords(searchQuery.trim(), displayLimit + 1) || []
    }

    readonly property var visibleWords: {
        if (probeWords.length > displayLimit)
            return probeWords.slice(0, displayLimit)
        return probeWords
    }

    readonly property bool hasMoreWords: probeWords.length > displayLimit

    readonly property int totalFilteredCount: {
        var _dep = Backend.dictionaryWords
        var q = searchQuery.trim()
        if (!q)
            return totalWordCount
        return Backend.countMatchingWords(q)
    }

    readonly property int remainingWordCount: Math.max(0, totalFilteredCount - visibleWords.length)

    ListModel {
        id: wordsModel
    }

    function syncWordsModel() {
        ListModelSync.syncByKey(
            wordsModel,
            visibleWords,
            function(w) { return String(w || "") },
            function(w) {
                const s = String(w || "")
                return { wordText: s, invalidWord: !Backend.isValidWord(s) || s.indexOf(" ") !== -1 }
            }
        )
    }

    onVisibleWordsChanged: syncWordsModel()
    Component.onCompleted: syncWordsModel()

    function boolSetting(key, fallback) {
        if (!Backend.values || Backend.values[key] === undefined)
            return fallback
        return Boolean(Backend.values[key])
    }

    LC.SettingsPage {
        // Section 1: Custom Dictionary Status
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Custom Dictionary")
                sectionId: "dict_status"
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: statusCol
                    width: parent.width
                    spacing: 2

                    LC.SettingRow {
                        Layout.fillWidth: true
                        title: I18n.tr("Enable Dictionary")
                        tooltipText: I18n.tr("Words in the custom dictionary are excluded from 'Auto Restore Invalid Words'. Use this for names, technical terms, or words missing from the built-in list.")

                        DW.DToggle {
                            checked: pageRoot.boolSetting("EnableDictionary", false)
                            onToggled: function(val) {
                                Backend.setValue("EnableDictionary", val)
                            }
                        }
                    }
                }
            }
        }

        // Section 2: Words
        ColumnLayout {
            Layout.fillWidth: true
            spacing: DC.Style.spacingS

            LC.SectionHeader {
                Layout.fillWidth: true
                title: I18n.tr("Words")
                sectionId: "dict_words"

                DW.DBadge {
                    text: pageRoot.searchQuery.trim().length > 0
                        ? (pageRoot.totalFilteredCount + " / " + pageRoot.totalWordCount)
                        : String(pageRoot.totalWordCount)
                    tooltipText: I18n.tr("Total words")
                }
            }

            DW.DCard {
                Layout.fillWidth: true
                pad: DC.Style.spacingS

                ColumnLayout {
                    id: wordsCol
                    width: parent.width
                    spacing: DC.Style.spacingS

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: DC.Style.spacingS

                        DW.DSearchField {
                            Layout.fillWidth: true
                            placeholderText: I18n.tr("Search words...")
                            onTextEdited: {
                                pageRoot.searchQuery = text
                                pageRoot.displayLimit = pageRoot.pageSize
                            }
                        }

                        DW.DButton {
                            text: I18n.tr("Add")
                            iconName: "add"
                            tooltipText: I18n.tr("Add Word")
                            onClicked: addWordDialog.open()
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
                        visible: pageRoot.visibleWords.length === 0
                        color: DC.Style.onSurfaceVariant
                        text: pageRoot.searchQuery.trim().length > 0
                            ? I18n.tr("No Matching Words")
                            : I18n.tr("Dictionary Empty")
                    }

                    Flow {
                        id: wordFlow
                        Layout.fillWidth: true
                        spacing: DC.Style.spacingS
                        visible: wordsModel.count > 0

                        Repeater {
                            model: wordsModel

                            DW.StyledButton {
                                id: wordChip
                                required property string wordText
                                required property bool invalidWord

                                implicitWidth: Math.min(wordFlow.width, wordLabel.implicitWidth + DC.Style.spacingM * 2)
                                implicitHeight: DC.Style.buttonHeightXS
                                radius: DC.Style.cornerRadiusS
                                color: invalidWord
                                    ? DC.Style.errorContainer
                                    : (chipState.hovered ? DC.Style.withAlpha(DC.Style.error, 0.12) : DC.Style.floatingWindowFieldColor)
                                border.width: DC.Style.outlineWidth
                                border.color: (invalidWord || chipState.hovered) ? DC.Style.error : DC.Style.floatingWindowFieldBorderColor
                                Accessible.role: Accessible.Button
                                Accessible.name: wordText + " — " + I18n.tr("Remove")
                                onClicked: Backend.removeWord(wordChip.wordText)

                                DW.FocusRing {
                                    visible: wordChip.visualFocus
                                    border.color: DC.Style.focusRingColor
                                    radius: wordChip.radius
                                }

                                DW.StyledText {
                                    id: wordLabel
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, Math.max(40, wordFlow.width - DC.Style.spacingM * 2))
                                    text: wordChip.wordText
                                    font.pixelSize: DC.Style.fontSizeSmall
                                    font.weight: DC.Style.fontWeightMedium
                                    color: wordChip.invalidWord
                                        ? DC.Style.onErrorContainer
                                        : (chipState.hovered ? DC.Style.error : DC.Style.surfaceText)
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                }

                                DW.StateLayer {
                                    id: chipState
                                    cornerRadius: wordChip.radius
                                    control: wordChip
                                    stateColor: DC.Style.error
                                    tooltipText: I18n.tr("Remove")
                                }
                            }
                        }
                    }

                    DW.DButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: pageRoot.hasMoreWords
                        variant: "tonal"
                        iconName: "expand_more"
                        text: I18n.tr("Show More (%1)").arg(pageRoot.remainingWordCount)
                        onClicked: pageRoot.displayLimit += pageRoot.pageSize
                    }
                }
            }
        }
    }

    LD.AddWordDialog {
        id: addWordDialog
        onWordAdded: function(word) {
            Backend.addWord(word)
        }
    }
}
