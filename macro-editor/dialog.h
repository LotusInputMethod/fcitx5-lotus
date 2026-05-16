/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _MACRO_EDITOR_DIALOG_H_
#define _MACRO_EDITOR_DIALOG_H_

#include "ui_dialog.h"
#include <QDialog>
#include <QString>
#include <QWidget>

namespace fcitx::unikey {

class MacroDialog : public QDialog, private Ui::Dialog {
    Q_OBJECT
public:
    explicit MacroDialog(QWidget *parent = nullptr);
    QString macro() const;
    QString word() const;
};
} // namespace fcitx::unikey

#endif // _MACRO_EDITOR_DIALOG_H_
