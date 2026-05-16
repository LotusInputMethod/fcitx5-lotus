/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "main.h"
#include "editor.h"
#include <QApplication>
#include <QObject>
#include <QtPlugin>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/macros.h>
#include <fcitxqtconfiguiplugin.h>
#include <fcitxqtconfiguiwidget.h>

namespace fcitx {

MacroEditorPlugin::MacroEditorPlugin(QObject *parent)
    : FcitxQtConfigUIPlugin(parent) {
    registerDomain("fcitx5-lotus", FCITX_INSTALL_LOCALEDIR);
}

FcitxQtConfigUIWidget *MacroEditorPlugin::create(const QString &key) {
    FCITX_UNUSED(key);
    return new fcitx::unikey::MacroEditor;
}

} // namespace fcitx
