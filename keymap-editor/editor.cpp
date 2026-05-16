/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "editor.h"
#include "actions.h"
#include "keycons.h"
#include "model.h"
#include <QAbstractItemModel>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QList>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QStandardItem>
#include <QWidget>
#include <Qt>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitxqtconfiguiwidget.h>
#include <fcitxqtkeysequencewidget.h>

namespace fcitx::unikey {

KeymapEditor::KeymapEditor(QWidget *parent) : FcitxQtConfigUIWidget(parent) {
    setupUi(this);

    keySequenceEdit->setKeycodeAllowed(false);
    keySequenceEdit->setModifierAllowed(false);
    keySequenceEdit->setModifierlessAllowed(true);

    connect(addButton, &QPushButton::clicked, this, &KeymapEditor::addKeymap);
    connect(moveUpButton, &QPushButton::clicked, this, [this]() {
        if (auto index = keymapView->currentIndex(); index.isValid()) {
            model_->moveUp(index.row());
        }
    });
    connect(moveDownButton, &QPushButton::clicked, this, [this]() {
        if (auto index = keymapView->currentIndex(); index.isValid()) {
            model_->moveDown(index.row());
        }
    });
    connect(deleteButton, &QPushButton::clicked, this,
            &KeymapEditor::deleteKeymap);
    connect(clearButton, &QPushButton::clicked, this,
            &KeymapEditor::deleteAllKeymap);
    connect(importButton, &QPushButton::clicked, this,
            &KeymapEditor::importKeymap);
    connect(exportButton, &QPushButton::clicked, this,
            &KeymapEditor::exportKeymap);

    inputMethodBox->addItem(_("Telex"), UkTelex);
    inputMethodBox->addItem(_("VNI"), UkVni);
    inputMethodBox->addItem(_("VIQR"), UkViqr);
    inputMethodBox->addItem(_("Microsoft Vietnamese"), UkMsVi);
    inputMethodBox->addItem(_("Simple Telex"), UkSimpleTelex);
    inputMethodBox->addItem(_("Simple Telex2"), UkSimpleTelex2);

    categoryBox->addItem(_("Tone marks"));
    categoryBox->addItem(_("Character complements"));
    categoryBox->addItem(_("Vietnamese characters"));

    for (const auto &[text, action, category] : actionNames()) {
        QStandardItem *item =
            new QStandardItem(QString::fromStdString(_(text)));
        item->setData(action, Qt::UserRole);
        item->setData(category, Qt::UserRole + 1);
        actionModel_.insertRow(actionModel_.rowCount(), item);
    }

    filteredActionModel_.setSourceModel(&actionModel_);

    actionBox->setModel(&filteredActionModel_);
    connect(categoryBox, qOverload<int>(&QComboBox::currentIndexChanged),
            &filteredActionModel_, &ActionFilterModel::setCategory);
    connect(categoryBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { actionBox->setCurrentIndex(0); });
    categoryBox->setCurrentIndex(0);

    model_ = new KeymapModel(this);
    keymapView->horizontalHeader()->setStretchLastSection(true);
    keymapView->verticalHeader()->setVisible(false);
    keymapView->setModel(model_);
    connect(keymapView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &KeymapEditor::itemFocusChanged);
    connect(model_, &QAbstractItemModel::rowsMoved, this,
            &KeymapEditor::itemFocusChanged);
    connect(model_, &KeymapModel::needSaveChanged, this,
            &KeymapEditor::changed);
    connect(keySequenceEdit, &FcitxQtKeySequenceWidget::keySequenceChanged,
            this, [this]() { addButton->setEnabled(keySequenceValid()); });

    load();
    itemFocusChanged();
    addButton->setEnabled(keySequenceValid());

    connect(loadButton, &QPushButton::clicked, this,
            [this]() { model_->load(inputMethodBox->currentData().toInt()); });
}

KeymapEditor::~KeymapEditor() {}

QString KeymapEditor::icon() { return "fcitx-lotus"; }

QString KeymapEditor::title() { return _("Lotus Keymap Editor"); }

void KeymapEditor::itemFocusChanged() {
    bool hasSelection = keymapView->currentIndex().isValid();
    deleteButton->setEnabled(hasSelection);
    moveUpButton->setEnabled(hasSelection &&
                             keymapView->currentIndex().row() > 0);
    moveDownButton->setEnabled(hasSelection &&
                               keymapView->currentIndex().row() + 1 <
                                   model_->rowCount());
    if (hasSelection) {
        auto chr = model_->index(keymapView->currentIndex().row(), 0)
                       .data(Qt::UserRole)
                       .toChar();
        keySequenceEdit->setKeySequence(
            QList<Key>() << Key(KeySym(chr.unicode()), KeyStates(), 0));
        auto action = model_->index(keymapView->currentIndex().row(), 1)
                          .data(Qt::UserRole)
                          .toInt();
        auto category = actionCategory(action);
        if (category >= 0) {
            categoryBox->setCurrentIndex(category);
            for (int i = 0; i < filteredActionModel_.rowCount(); i++) {
                if (auto index = filteredActionModel_.index(i, 0);
                    index.data(Qt::UserRole) == action) {
                    actionBox->setCurrentIndex(i);
                }
            }
        }
    }
}

bool KeymapEditor::keySequenceValid() const {
    if (keySequenceEdit->keySequence().empty()) {
        return false;
    }
    auto key = keySequenceEdit->keySequence()[0];
    return key.isValid() && key.isSimple();
}

void KeymapEditor::deleteKeymap() {
    if (!keymapView->currentIndex().isValid()) {
        return;
    }
    int row = keymapView->currentIndex().row();
    model_->deleteItem(row);
}

void KeymapEditor::deleteAllKeymap() { model_->deleteAllItem(); }

void KeymapEditor::addKeymap() {
    if (!keySequenceValid()) {
        return;
    }
    auto action = actionBox->currentData(Qt::UserRole);
    if (!action.isValid()) {
        return;
    }
    auto key = keySequenceEdit->keySequence()[0];
    unsigned char chr = key.sym() & 0xff;

    auto index = model_->addItem(chr, action.toInt());
    keymapView->setCurrentIndex(index);
}

void KeymapEditor::load() { model_->load(); }

void KeymapEditor::save() { model_->save(); }

void KeymapEditor::importKeymap() {
    QFileDialog *dialog = new QFileDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setFileMode(QFileDialog::ExistingFile);
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->open();
    connect(dialog, &QFileDialog::accepted, this,
            &KeymapEditor::importFileSelected);
}

void KeymapEditor::importFileSelected() {
    const QFileDialog *dialog =
        qobject_cast<const QFileDialog *>(QObject::sender());
    if (dialog->selectedFiles().isEmpty()) {
        return;
    }
    QString file = dialog->selectedFiles()[0];
    model_->load(file);
}

void KeymapEditor::exportKeymap() {
    QFileDialog *dialog = new QFileDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->open();
    connect(dialog, &QFileDialog::accepted, this,
            &KeymapEditor::exportFileSelected);
}

void KeymapEditor::exportFileSelected() {
    const QFileDialog *dialog =
        qobject_cast<const QFileDialog *>(QObject::sender());
    if (dialog->selectedFiles().length() <= 0) {
        return;
    }
    QString file = dialog->selectedFiles()[0];
    model_->save(file);
}

} // namespace fcitx::unikey
