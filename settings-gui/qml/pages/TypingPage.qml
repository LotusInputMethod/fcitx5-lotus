// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import DCommon.Common
import DCommon.Widgets
import "../components"

SettingsPage {
    id: root

    function boolVal(key, fallback) {
        const v = Backend.values[key];
        if (v === undefined || v === null)
            return fallback;
        if (typeof v === "boolean")
            return v;
        return String(v).toLowerCase() === "true";
    }

    // Spelling
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Spelling")
            sectionId: "spelling"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: I18n.tr("Spell Check")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("SpellCheck", true)
                        onToggled: val => Backend.setValue("SpellCheck", val)
                    }
                }

                SettingRow {
                    title: I18n.tr("Auto Restore Invalid Words")
                    tooltipText: I18n.tr("Automatically revert the typed sequence if the resulting word is not in the dictionary.\nThis helps prevent accidental Vietnamese transformations on English words or mixed text.")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("AutoNonVnRestore", true)
                        onToggled: val => Backend.setValue("AutoNonVnRestore", val)
                    }
                }

                SettingRow {
                    title: I18n.tr("Free dd to đ")
                    tooltipText: I18n.tr("Allow dd to produce đ when Auto Restore Invalid Words is enabled.")

                    DToggle {
                        hideText: true
                        checked: root.boolVal("DdFreeStyle", true)
                        onToggled: val => Backend.setValue("DdFreeStyle", val)
                    }
                }
            }
        }
    }

    // Tone & Punctuation
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Tone & Punctuation")
            sectionId: "tone_punctuation"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: I18n.tr("Modern Tone Style")
                    tooltipText: I18n.tr("Place tone marks on newer vowel positions (oà, uý instead of òa, úy).")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("ModernStyle", true)
                        onToggled: val => Backend.setValue("ModernStyle", val)
                    }
                }

                SettingRow {
                    title: I18n.tr("Free Tone Marking")
                    tooltipText: I18n.tr("You can type tone marks at the end of the word or anywhere inside.")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("FreeMarking", true)
                        onToggled: val => Backend.setValue("FreeMarking", val)
                    }
                }

                SettingRow {
                    title: I18n.tr("Auto Capitalize After Punctuation")
                    tooltipText: I18n.tr("Capitalize the next word after sentence-ending punctuation (. ! ? Enter).")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("AutoCapitalizeAfterPunctuation", false)
                        onToggled: val => Backend.setValue("AutoCapitalizeAfterPunctuation", val)
                    }
                }

                SettingRow {
                    title: I18n.tr("Double Space to Period")
                    tooltipText: I18n.tr("Insert a period followed by a space when pressing Space twice.")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("DoubleSpaceToPeriod", false)
                        onToggled: val => Backend.setValue("DoubleSpaceToPeriod", val)
                    }
                }

                SettingRow {
                    title: I18n.tr("Double Hyphen to Em-Dash (--)")
                    tooltipText: I18n.tr("Transform -- into an em-dash (—).")

                    DToggle {
                        hideText: true
                        checked: root.boolVal("DoubleHyphenToEmDash", false)
                        onToggled: val => Backend.setValue("DoubleHyphenToEmDash", val)
                    }
                }
            }
        }
    }
}
