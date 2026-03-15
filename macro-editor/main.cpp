/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "main.h"
#include "editor.h"

namespace fcitx::lotus {
    MacroEditorPlugin::MacroEditorPlugin(QObject* parent) : FcitxQtConfigUIPlugin(parent) {}

    FcitxQtConfigUIWidget* MacroEditorPlugin::create(const QString& key) {
        if (key == "lotus-macro") {
            auto* editor = new MacroEditor; //NOLINT
            editor->load();
            return editor;
        }
        return nullptr;
    }
} // namespace fcitx::lotus
