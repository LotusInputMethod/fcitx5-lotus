/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "main.h"
#include "editor.h"
#include <QApplication>
#include <fcitx-utils/i18n.h>
#include <qplugin.h>

namespace fcitx {

KeymapEditorPlugin::KeymapEditorPlugin(QObject *parent)
    : FcitxQtConfigUIPlugin(parent) {
    registerDomain("fcitx5-lotus", FCITX_INSTALL_LOCALEDIR);
}

FcitxQtConfigUIWidget *KeymapEditorPlugin::create(const QString &key) {
    Q_UNUSED(key);
    return new fcitx::unikey::KeymapEditor;
}

} // namespace fcitx
