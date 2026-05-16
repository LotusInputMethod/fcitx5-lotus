/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _KEYMAP_EDITOR_MAIN_H_
#define _KEYMAP_EDITOR_MAIN_H_

#include <fcitxqtconfiguiplugin.h>

namespace fcitx {

class KeymapEditorPlugin : public FcitxQtConfigUIPlugin {
    Q_OBJECT
public:
    Q_PLUGIN_METADATA(IID FcitxQtConfigUIFactoryInterface_iid FILE
                      "keymap-editor.json")
    explicit KeymapEditorPlugin(QObject *parent = 0);
    FcitxQtConfigUIWidget *create(const QString &key) override;
};

} // namespace fcitx

#endif // _KEYMAP_EDITOR_MAIN_H_
