/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "editor.h"
#include "charset.h"
#include "dialog.h"
#include "keycons.h"
#include "mactab.h"
#include "model.h"
#include "vnconv.h"
#include <QCloseEvent>
#include <QDebug>
#include <QDialog>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QWidget>
#include <Qt>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitxqtconfiguiwidget.h>
#include <memory>

namespace fcitx::unikey {

MacroEditor::MacroEditor(QWidget *parent)
    : FcitxQtConfigUIWidget(parent), table_(std::make_unique<CMacroTable>()),
      model_(new MacroModel(this)) {
    setupUi(this);

    connect(addButton, &QPushButton::clicked, this, &MacroEditor::addWord);
    connect(deleteButton, &QPushButton::clicked, this,
            &MacroEditor::deleteWord);
    connect(clearButton, &QPushButton::clicked, this,
            &MacroEditor::deleteAllWord);
    connect(importButton, &QPushButton::clicked, this,
            &MacroEditor::importMacro);
    connect(exportButton, &QPushButton::clicked, this,
            &MacroEditor::exportMacro);
    table_->init();
    macroTableView->horizontalHeader()->setStretchLastSection(true);
    macroTableView->verticalHeader()->setVisible(false);
    macroTableView->setModel(model_);
    connect(macroTableView->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            &MacroEditor::itemFocusChanged);
    connect(model_, &MacroModel::needSaveChanged, this, &MacroEditor::changed);
    load();
    itemFocusChanged();
}

MacroEditor::~MacroEditor() {}

QString MacroEditor::icon() { return "fcitx-lotus"; }

QString MacroEditor::title() { return _("Lotus Macro Editor"); }

void MacroEditor::itemFocusChanged() {
    deleteButton->setEnabled(macroTableView->currentIndex().isValid());
}

void MacroEditor::deleteWord() {
    if (!macroTableView->currentIndex().isValid()) {
        return;
    }
    int row = macroTableView->currentIndex().row();
    model_->deleteItem(row);
}

void MacroEditor::deleteAllWord() { model_->deleteAllItem(); }

void MacroEditor::addWord() {
    auto *dialog = new MacroDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->open();
    connect(dialog, &QDialog::accepted, this, &MacroEditor::addWordAccepted);
}

QString MacroEditor::getData(CMacroTable *table, int i, bool iskey) {

    char key[MAX_MACRO_KEY_LEN * 3];
    char value[MAX_MACRO_TEXT_LEN * 3];
    do {
        if (i < table->getCount()) {
            const StdVnChar *p = nullptr;
            int maxOutLen = 0;
            const char *result = nullptr;
            if (iskey) {
                p = table->getKey(i);
                maxOutLen = sizeof(key);
                result = key;
            } else {
                p = table->getText(i);
                maxOutLen = sizeof(value);
                result = value;
            }

            if (!p) {
                break;
            }
            int inLen = -1;
            int ret =
                VnConvert(CONV_CHARSET_VNSTANDARD, CONV_CHARSET_XUTF8,
                          (UKBYTE *)p, (UKBYTE *)result, &inLen, &maxOutLen);
            if (ret != 0) {
                break;
            }
            return QString::fromUtf8(result);
        }
    } while (0);
    return QString();
}

void MacroEditor::addWordAccepted() {
    const auto *dialog = qobject_cast<const MacroDialog *>(QObject::sender());

    model_->addItem(dialog->macro(), dialog->word());
}

void MacroEditor::load() {
    auto path = StandardPaths::global().locate(StandardPathsType::PkgConfig,
                                               "lotus/macro");
    table_->loadFromFile(path.string().c_str());
    model_->load(table_.get());
}

void MacroEditor::save() {
    model_->save(table_.get());
    StandardPaths::global().safeSave(StandardPathsType::PkgConfig,
                                     "lotus/macro", [this](int fd) -> bool {
                                         UnixFD unixFD(fd);
                                         auto f = fs::openFD(unixFD, "wb");
                                         return table_->writeToFp(f.release());
                                     });
}

void MacroEditor::importMacro() {
    auto *dialog = new QFileDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setFileMode(QFileDialog::ExistingFile);
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->open();
    connect(dialog, &QFileDialog::accepted, this,
            &MacroEditor::importFileSelected);
}

void MacroEditor::importFileSelected() {
    const auto *dialog = qobject_cast<const QFileDialog *>(QObject::sender());
    if (dialog->selectedFiles().length() <= 0) {
        return;
    }
    QString file = dialog->selectedFiles()[0];
    table_->loadFromFile(file.toUtf8().constData());
}

void MacroEditor::exportMacro() {
    auto *dialog = new QFileDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setDirectory("macro");
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->open();
    connect(dialog, &QFileDialog::accepted, this,
            &MacroEditor::exportFileSelected);
}

void MacroEditor::exportFileSelected() {
    const auto *dialog = qobject_cast<const QFileDialog *>(QObject::sender());
    if (dialog->selectedFiles().length() <= 0) {
        return;
    }
    QString file = dialog->selectedFiles()[0];
    table_->writeToFile(file.toUtf8().constData());
}

} // namespace fcitx::unikey
