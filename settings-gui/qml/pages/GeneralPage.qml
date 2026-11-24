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

    function enumLabels(key) {
        const _v = Backend.values;
        const opts = Backend.getEnumOptions(key) || [];
        return opts.map(o => String(o.label ?? o.value ?? ""));
    }

    function enumValues(key) {
        const _v = Backend.values;
        const opts = Backend.getEnumOptions(key) || [];
        return opts.map(o => String(o.value ?? ""));
    }

    function strVal(key, fallback) {
        const v = Backend.values[key];
        return v !== undefined && v !== null ? String(v) : fallback;
    }

    function boolVal(key, fallback) {
        const v = Backend.values[key];
        if (v === undefined || v === null)
            return fallback;
        if (typeof v === "boolean")
            return v;
        return String(v).toLowerCase() === "true";
    }

    // Input
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Input")
            sectionId: "input"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: Backend.optionTitle("InputMethod")
                    showSeparator: true

                    DDropdown {
                        compactMode: true
                        dropdownWidth: 210
                        options: root.enumLabels("InputMethod")
                        optionValues: root.enumValues("InputMethod")
                        Binding on currentValue {
                            value: root.strVal("InputMethod", "")
                        }
                        onValueChanged: val => Backend.setValue("InputMethod", val)
                    }
                }

                SettingRow {
                    title: Backend.optionTitle("Mode")
                    showSeparator: true

                    DDropdown {
                        compactMode: true
                        dropdownWidth: 210
                        options: root.enumLabels("Mode")
                        optionValues: root.enumValues("Mode")
                        Binding on currentValue {
                            value: root.strVal("Mode", "")
                        }
                        onValueChanged: val => Backend.setValue("Mode", val)
                    }
                }

                SettingRow {
                    title: Backend.optionTitle("OutputCharset")

                    DDropdown {
                        compactMode: true
                        dropdownWidth: 210
                        options: root.enumLabels("OutputCharset")
                        optionValues: root.enumValues("OutputCharset")
                        Binding on currentValue {
                            value: root.strVal("OutputCharset", "")
                        }
                        onValueChanged: val => Backend.setValue("OutputCharset", val)
                    }
                }
            }
        }
    }

    // Quick Keys
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Quick Keys")
            sectionId: "quick_keys"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: Backend.optionTitle("W2U")
                    showSeparator: true

                    DDropdown {
                        compactMode: true
                        dropdownWidth: 210
                        options: root.enumLabels("W2U")
                        optionValues: root.enumValues("W2U")
                        Binding on currentValue {
                            value: root.strVal("W2U", String(Backend.optionDefault("W2U")))
                        }
                        onValueChanged: val => Backend.setValue("W2U", val)
                    }
                }

                SettingRow {
                    title: Backend.optionTitle("BracketTransform")
                    tooltipText: I18n.tr("Transform [ to ơ, ] to ư, { to Ơ, and } to Ư.")

                    DDropdown {
                        compactMode: true
                        dropdownWidth: 210
                        options: root.enumLabels("BracketTransform")
                        optionValues: root.enumValues("BracketTransform")
                        Binding on currentValue {
                            value: root.strVal("BracketTransform", String(Backend.optionDefault("BracketTransform")))
                        }
                        onValueChanged: val => Backend.setValue("BracketTransform", val)
                    }
                }
            }
        }
    }

    // Backend
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Backend")
            sectionId: "backend"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: Backend.optionTitle("PreferVirtualKeyboard")
                    tooltipText: I18n.tr("Prefer injecting keys through the system instead of the uinput service: the wlroots virtual keyboard on Wayland and Xtest on X11.\nTurn this off to always use the uinput service; when neither backend is available the uinput service is used anyway.")

                    DToggle {
                        hideText: true
                        checked: root.boolVal("PreferVirtualKeyboard", Boolean(Backend.optionDefault("PreferVirtualKeyboard")))
                        onToggled: val => Backend.setValue("PreferVirtualKeyboard", val)
                    }
                }
            }
        }
    }

    // Tray Icon
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Style.spacingS

        SectionHeader {
            title: I18n.tr("Tray Icon")
            sectionId: "tray_icon"
        }

        DCard {
            Layout.fillWidth: true
            pad: Style.spacingXS

            ColumnLayout {
                width: parent.width
                spacing: Style.spacingXXS

                SettingRow {
                    title: Backend.optionTitle("UseLotusIcons")
                    showSeparator: true

                    DToggle {
                        hideText: true
                        checked: root.boolVal("UseLotusIcons", Boolean(Backend.optionDefault("UseLotusIcons")))
                        onToggled: val => Backend.setValue("UseLotusIcons", val)
                    }
                }

                SettingRow {
                    title: Backend.optionTitle("IconTheme")

                    DDropdown {
                        compactMode: true
                        dropdownWidth: 210
                        options: root.enumLabels("IconTheme")
                        optionValues: root.enumValues("IconTheme")
                        Binding on currentValue {
                            value: root.strVal("IconTheme", String(Backend.optionDefault("IconTheme")))
                        }
                        onValueChanged: val => Backend.setValue("IconTheme", val)
                    }
                }
            }
        }
    }
}
