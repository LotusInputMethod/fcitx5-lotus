/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include <QApplication>

#include "editor.h"
#include "model.h"
#include "vnconv.h"
#include <QAbstractTableModel>
#include <QObject>
#include <QString>
#include <QVariant>
#include <Qt>
#include <fcitx-utils/i18n.h>
#include <utility>

namespace fcitx::unikey {

using ItemType = std::pair<QString, QString>;

MacroModel::MacroModel(QObject *parent)
    : QAbstractTableModel(parent), needSave_(false) {}

MacroModel::~MacroModel() {}

QVariant MacroModel::headerData(int section, Qt::Orientation orientation,
                                int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0) {
            return _("Macro");
        }
        if (section == 1) {
            return _("Word");
        }
    }
    return {};
}

int MacroModel::rowCount(const QModelIndex & /*parent*/) const {
    return list_.count();
}

int MacroModel::columnCount(const QModelIndex & /*parent*/) const { return 2; }

QVariant MacroModel::data(const QModelIndex &index, int role) const {
    do {
        if (role == Qt::DisplayRole && index.row() < list_.count()) {
            if (index.column() == 0) {
                return list_[index.row()].first;
            }
            if (index.column() == 1) {
                return list_[index.row()].second;
            }
        }
    } while (0);
    return QVariant();
}

void MacroModel::addItem(const QString &macro, const QString &word) {
    if (keyset_.contains(macro)) {
        return;
    }
    beginInsertRows(QModelIndex(), list_.size(), list_.size());
    list_.append(std::pair<QString, QString>(macro, word));
    keyset_.insert(macro);
    endInsertRows();
    setNeedSave(true);
}

void MacroModel::deleteItem(int row) {
    if (row >= list_.count()) {
        return;
    }
    std::pair<QString, QString> item = list_.at(row);
    QString key = item.first;
    beginRemoveRows(QModelIndex(), row, row);
    list_.removeAt(row);
    keyset_.remove(key);
    endRemoveRows();
    setNeedSave(true);
}

void MacroModel::deleteAllItem() {
    if (list_.count()) {
        setNeedSave(true);
    }
    beginResetModel();
    list_.clear();
    keyset_.clear();
    endResetModel();
}

void MacroModel::setNeedSave(bool needSave) {
    if (needSave_ != needSave) {
        needSave_ = needSave;
        Q_EMIT needSaveChanged(needSave_);
    }
}

bool MacroModel::needSave() const { return needSave_; }

void MacroModel::load(CMacroTable *table) {
    beginResetModel();
    list_.clear();
    keyset_.clear();
    for (int i = 0; i < table->getCount(); i++) {
        QString key = MacroEditor::getData(table, i, true);
        QString value = MacroEditor::getData(table, i, false);
        list_.append(std::pair<QString, QString>(key, value));
        keyset_.insert(key);
    }
    endResetModel();
}

void MacroModel::save(CMacroTable *table) {
    table->resetContent();
    for (const ItemType &item : list_) {
        table->addItem(item.first.toUtf8().data(), item.second.toUtf8().data(),
                       CONV_CHARSET_XUTF8);
    }
    setNeedSave(false);
}

} // namespace fcitx::unikey
