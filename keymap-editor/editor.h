/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _KEYMAP_EDITOR_EDITOR_H_
#define _KEYMAP_EDITOR_EDITOR_H_

#include "ui_editor.h"
#include <QDebug>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <fcitxqtconfiguiwidget.h>

class CKeymapTable;

namespace fcitx {
namespace unikey {

class ActionFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public Q_SLOTS:
    void setCategory(int category) {
        category_ = category;
        invalidate();
    }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override {
        const QModelIndex index =
            sourceModel()->index(sourceRow, 0, sourceParent);
        return index.data(Qt::UserRole + 1) == category_;
    }

private:
    int category_ = 0;
};

class KeymapModel;
class KeymapEditor : public FcitxQtConfigUIWidget, public Ui::Editor {
    Q_OBJECT
public:
    explicit KeymapEditor(QWidget *parent = 0);
    virtual ~KeymapEditor();
    void load() override;
    void save() override;
    QString title() override;
    QString icon() override;

    static QString getData(CKeymapTable *table, int i, bool iskey);
private Q_SLOTS:
    void addKeymap();
    void deleteKeymap();
    void deleteAllKeymap();
    void itemFocusChanged();
    bool keySequenceValid() const;
    void importKeymap();
    void exportKeymap();
    void importFileSelected();
    void exportFileSelected();

private:
    CKeymapTable *table_;
    KeymapModel *model_;
    QStandardItemModel actionModel_;
    ActionFilterModel filteredActionModel_;
};
} // namespace unikey
} // namespace fcitx

#endif // _MACRO_EDITOR_EDITOR_H_
