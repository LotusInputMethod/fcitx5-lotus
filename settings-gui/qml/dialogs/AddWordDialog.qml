// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import "../DCommon/Widgets" as DW
import "../components" as LC

LC.OverlayDialog {
    id: root

    maximumWidth: 420
    title: I18n.tr("Add Word")
    supportingText: I18n.tr("Enter a word without spaces:")
    acceptEnabled: canAdd

    readonly property string trimmedWord: wordField.text.trim()
    readonly property bool hasSpaces: trimmedWord.length > 0 && (trimmedWord.indexOf(" ") !== -1 || !Backend.isValidWord(trimmedWord))
    readonly property bool alreadyExists: {
        if (trimmedWord.length === 0)
            return false
        var words = Backend.dictionaryWords || []
        return words.indexOf(trimmedWord) !== -1
    }
    readonly property string errorMsg: hasSpaces
        ? I18n.tr("Words cannot contain spaces")
        : (alreadyExists ? I18n.tr("Word already exists") : "")
    readonly property bool canAdd: trimmedWord.length > 0 && !hasSpaces && !alreadyExists

    signal wordAdded(string word)

    function open() {
        wordField.text = ""
        opened = true
        wordField.forceActiveFocus()
    }

    function submit() {
        if (!canAdd)
            return
        wordAdded(trimmedWord)
        close()
    }

    onAccepted: submit()

    DW.DTextField {
        id: wordField
        width: parent.width
        outlined: true
        placeholderText: I18n.tr("e.g. khongdau")
        isError: root.errorMsg !== ""
        supportingText: root.errorMsg
        onAccepted: root.submit()
    }

    actions: [
        DW.DButton {
            text: I18n.tr("Cancel")
            variant: "text"
            onClicked: root.close()
        },
        DW.DButton {
            text: I18n.tr("Add")
            enabled: root.canAdd
            onClicked: root.submit()
        }
    ]
}
