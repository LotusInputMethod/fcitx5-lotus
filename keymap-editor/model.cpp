/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include <QApplication>

#include "actions.h"
#include "editor.h"
#include "inputproc.h"
#include "keycons.h"
#include "model.h"
#include "usrkeymap.h"
#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <Qt>
#include <algorithm>
#include <cstddef>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/unixfd.h>
#include <fcntl.h>
#include <iterator>
#include <utility>

namespace fcitx::unikey {

using ItemType = std::pair<QString, QString>;

KeymapModel::KeymapModel(QObject *parent)
    : QAbstractTableModel(parent), needSave_(false) {}

KeymapModel::~KeymapModel() {}

QVariant KeymapModel::headerData(int section, Qt::Orientation orientation,
                                 int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0) {
            return _("Keymap");
        }
        if (section == 1) {
            return _("Word");
        }
    }
    return {};
}

int KeymapModel::rowCount(const QModelIndex & /*parent*/) const {
    return list_.size();
}

int KeymapModel::columnCount(const QModelIndex & /*parent*/) const { return 2; }

QVariant KeymapModel::data(const QModelIndex &index, int role) const {
    if (index.row() >= static_cast<int>(list_.size()) || index.row() < 0) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) {
            return QString(QChar(list_[index.row()].key));
        }
        if (index.column() == 1) {
            return QString::fromStdString(
                _(actionName(list_[index.row()].action)));
        }
    } else if (role == Qt::UserRole) {
        if (index.column() == 0) {
            return QChar(list_[index.row()].key);
        }
        if (index.column() == 1) {
            return list_[index.row()].action;
        }
    }
    return QVariant();
}

QModelIndex KeymapModel::addItem(unsigned char key, int action) {
    beginResetModel();
    bool checkBoth = false;
    if (action < vneCount) {
        key = charutils::toupper(key);
        checkBoth = true;
    }
    const unsigned char lower = charutils::tolower(key);
    bool updated = false;
    auto match = [key, checkBoth, lower](const UkKeyMapping &item) {
        if (item.action < vneCount &&
            charutils::toupper(item.key) == charutils::toupper(key)) {
            return true;
        }
        return item.key == key || (checkBoth && item.key == lower);
    };

    auto iter = list_.begin();
    for (; iter != list_.end(); iter++) {
        if (match(*iter)) {
            *iter = UkKeyMapping{.key = key, .action = action};
            updated = true;
            break;
        }
    }

    int selectRow = 0;
    if (updated) {
        selectRow = std::distance(list_.begin(), iter);
        list_.erase(std::remove_if(std::next(iter), list_.end(), match),
                    list_.end());
    } else {
        selectRow = list_.size();
        list_.push_back(UkKeyMapping{key, action});
    }
    endResetModel();
    setNeedSave(true);
    return index(selectRow, 0);
}

void KeymapModel::moveUp(int row) {
    if (row >= static_cast<int>(list_.size()) || row <= 0) {
        return;
    }
    if (!beginMoveRows(QModelIndex(), row, row, QModelIndex(), row - 1)) {
        return;
    }
    std::swap(list_[row - 1], list_[row]);
    endMoveRows();
    setNeedSave(true);
}

void KeymapModel::moveDown(int row) {
    if (row + 1 >= static_cast<int>(list_.size()) || row < 0) {
        return;
    }
    if (!beginMoveRows(QModelIndex(), row, row, QModelIndex(), row + 2)) {
        return;
    }
    std::swap(list_[row], list_[row + 1]);
    endMoveRows();
    setNeedSave(true);
}

void KeymapModel::deleteItem(int row) {
    if (row >= static_cast<int>(list_.size())) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    list_.erase(list_.begin() + row);
    endRemoveRows();
    setNeedSave(true);
}

void KeymapModel::deleteAllItem() {
    if (!list_.empty()) {
        setNeedSave(true);
    }
    beginResetModel();
    list_.clear();
    endResetModel();
}

void KeymapModel::setNeedSave(bool needSave) {
    if (needSave_ != needSave) {
        needSave_ = needSave;
        Q_EMIT needSaveChanged(needSave_);
    }
}

bool KeymapModel::needSave() const { return needSave_; }

void KeymapModel::load() {
    beginResetModel();
    auto keymapFile = StandardPaths::global().open(StandardPathsType::PkgConfig,
                                                   "lotus/keymap.txt");
    if (keymapFile.isValid()) {
        list_ = UkLoadKeyOrderMap(keymapFile.fd());
    } else {
        list_.clear();
    }
    endResetModel();
}

void KeymapModel::save() {
    StandardPaths::global().safeSave(StandardPathsType::PkgConfig,
                                     "lotus/keymap.txt",
                                     [this](int fd) { return saveToFd(fd); });
    setNeedSave(false);
}

void KeymapModel::load(const QString &file) {
    UnixFD fd = UnixFD::own(open(file.toLocal8Bit().constData(), O_RDONLY));

    if (!fd.isValid()) {
        return;
    }

    beginResetModel();
    list_ = UkLoadKeyOrderMap(fd.fd());
    endResetModel();
    setNeedSave(true);
}

void KeymapModel::save(const QString &file) {
    if (!file.startsWith("/")) {
        return;
    }
    StandardPaths::global().safeSave(StandardPathsType::PkgConfig,
                                     file.toLocal8Bit().constData(),
                                     [this](int fd) { return saveToFd(fd); });
    setNeedSave(false);
}

bool KeymapModel::saveToFd(int fd) {
    UnixFD unixFD(fd);
    auto fp = fs::openFD(unixFD, "wb");
    if (!fp) {
        return false;
    }
    UkStoreKeyOrderMap(fp.get(), list_);
    return true;
}

void KeymapModel::load(int profile) {
    const UkKeyMapping *mapping = nullptr;
    switch (profile) {
    case UkTelex:
        mapping = TelexMethodMapping;
        break;
    case UkSimpleTelex:
        mapping = SimpleTelexMethodMapping;
        break;
    case UkSimpleTelex2:
        mapping = SimpleTelex2MethodMapping;
        break;
    case UkVni:
        mapping = VniMethodMapping;
        break;
    case UkViqr:
        mapping = VIQRMethodMapping;
        break;
    case UkMsVi:
        mapping = MsViMethodMapping;
        break;
    default:
        break;
    }
    if (!mapping) {
        return;
    }

    beginResetModel();
    list_.clear();
    for (size_t i = 0; mapping[i].key != 0; i++) {
        list_.push_back(mapping[i]);
    }
    endResetModel();
    setNeedSave(true);
}

} // namespace fcitx::unikey
