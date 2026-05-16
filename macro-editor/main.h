/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _MACRO_EDITOR_MAIN_H_
#define _MACRO_EDITOR_MAIN_H_

#include <QObject>
#include <QString>
#include <fcitxqtconfiguiplugin.h>

namespace fcitx {

class MacroEditorPlugin : public FcitxQtConfigUIPlugin {
    Q_OBJECT
public:
    Q_PLUGIN_METADATA(IID FcitxQtConfigUIFactoryInterface_iid FILE
                      "macro-editor.json")
    explicit MacroEditorPlugin(QObject *parent = 0);
    FcitxQtConfigUIWidget *create(const QString &key) override;
};

} // namespace fcitx

#endif // _MACRO_EDITOR_MAIN_H_
