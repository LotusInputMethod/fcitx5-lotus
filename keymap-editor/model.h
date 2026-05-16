/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _KEYMAP_EDITOR_MODEL_H_
#define _KEYMAP_EDITOR_MODEL_H_

#include "inputproc.h"
#include <QAbstractItemModel>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariant>
#include <Qt>
#include <vector>

namespace fcitx::unikey {
class KeymapModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit KeymapModel(QObject *parent = 0);
    virtual ~KeymapModel();

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    void load();
    QModelIndex addItem(unsigned char key, int action);
    void moveUp(int row);
    void moveDown(int row);
    void deleteItem(int row);
    void deleteAllItem();
    void save();
    bool needSave() const;
    void load(const QString &fileName);
    void save(const QString &fileName);
    void load(int profile);

Q_SIGNALS:
    void needSaveChanged(bool);

private:
    void setNeedSave(bool needSave);
    bool saveToFd(int fd);
    bool needSave_;
    std::vector<UkKeyMapping> list_;
};
} // namespace fcitx::unikey

#endif // _MACRO_EDITOR_MODEL_H_
