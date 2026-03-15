/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "editor.h"
#include "lotus-config.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QTextStream>
#include <qtmetamacros.h>

// NOLINTBEGIN(cppcoreguidelines-owning-memory)
namespace fcitx::lotus {
    KeymapEditor::KeymapEditor(QWidget* parent) :
        FcitxQtConfigUIWidget(parent), tableWidget_(new QTableWidget(0, 2, this)), inputKey_(new QLineEdit(this)), comboAction_(new QComboBox(this)),
        comboPreset_(new QComboBox(this)) {
        auto* mainLayout = new QVBoxLayout(this);

        auto* presetLayout = new QHBoxLayout();

        for (const auto& preset : presets_) {
            comboPreset_->addItem(preset.first);
        }
        btnLoadPreset_ = new QPushButton(QIcon::fromTheme("document-import"), _("Import From Existing Keymap"), this);

        presetLayout->addWidget(new QLabel(_("Original Input Method"), this));
        presetLayout->addWidget(comboPreset_);
        presetLayout->addWidget(btnLoadPreset_);
        presetLayout->addStretch();

        mainLayout->addLayout(presetLayout);

        QFrame* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        mainLayout->addWidget(line);

        auto* addLayout = new QHBoxLayout();
        inputKey_->setPlaceholderText(_("Key (Example: s)"));
        inputKey_->setMaxLength(1);

        for (const auto& action : bambooActions_) {
            comboAction_->addItem(action.second, action.first);
        }

        btnAdd_ = new QPushButton(QIcon::fromTheme("list-add"), "", this);
        btnAdd_->setFixedSize(30, 30);
        btnAdd_->setAutoDefault(false);
        btnRemove_ = new QPushButton(QIcon::fromTheme("list-remove"), "", this);
        btnRemove_->setFixedSize(30, 30);
        btnRemove_->setAutoDefault(false);

        addLayout->addWidget(inputKey_);
        addLayout->addWidget(comboAction_);
        addLayout->addWidget(btnAdd_);
        addLayout->addWidget(btnRemove_);
        mainLayout->addLayout(addLayout);

        tableWidget_->setHorizontalHeaderLabels({_("Key"), _("Action")});
        tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget_->setAlternatingRowColors(true);
        mainLayout->addWidget(tableWidget_);

        // ── Import / Export row ────────────────────────────────────────────────
        auto* ioLayout = new QHBoxLayout();
        btnImport_     = new QPushButton(QIcon::fromTheme("document-import"), _("Import"), this);
        btnExport_     = new QPushButton(QIcon::fromTheme("document-export"), _("Export"), this);

        QString ioBtnStyle = "QPushButton { text-align: left; padding: 6px 12px; }";
        btnImport_->setStyleSheet(ioBtnStyle);
        btnExport_->setStyleSheet(ioBtnStyle);

        ioLayout->addWidget(btnImport_);
        ioLayout->addWidget(btnExport_);
        ioLayout->addStretch();

        mainLayout->addLayout(ioLayout);

        connect(btnAdd_, &QPushButton::clicked, this, &KeymapEditor::onAddClicked);
        connect(btnRemove_, &QPushButton::clicked, this, &KeymapEditor::onRemoveClicked);
        connect(btnLoadPreset_, &QPushButton::clicked, this, &KeymapEditor::onLoadPresetClicked);
        connect(btnImport_, &QPushButton::clicked, this, &KeymapEditor::onImportClicked);
        connect(btnExport_, &QPushButton::clicked, this, &KeymapEditor::onExportClicked);
        connect(tableWidget_, &QTableWidget::cellClicked, this, &KeymapEditor::onRowSelected);

        inputKey_->setFocus();
    }

    QString KeymapEditor::title() {
        return _("Lotus Custom Keymap");
    }

    QString KeymapEditor::icon() {
        return "fcitx-lotus";
    }

    void KeymapEditor::onAddClicked() {
        QString key = inputKey_->text().trimmed();
        if (key.isEmpty())
            return;

        for (int i = 0; i < tableWidget_->rowCount(); ++i) {
            if (tableWidget_->item(i, 0)->text() == key) {
                auto* cellCombo = qobject_cast<QComboBox*>(tableWidget_->cellWidget(i, 1));
                if (cellCombo != nullptr) {
                    cellCombo->setCurrentIndex(comboAction_->currentIndex());
                }
                emit changed(true);
                return;
            }
        }

        int row = tableWidget_->rowCount();
        tableWidget_->insertRow(row);
        tableWidget_->setItem(row, 0, new QTableWidgetItem(key));

        auto* cellCombo = new QComboBox();
        for (const auto& action : bambooActions_) {
            cellCombo->addItem(action.second, action.first);
        }
        cellCombo->setCurrentIndex(comboAction_->currentIndex());

        connect(cellCombo, &QComboBox::currentIndexChanged, this, [this]() { emit changed(true); });

        tableWidget_->setCellWidget(row, 1, cellCombo);
        emit changed(true);
    }

    void KeymapEditor::onRemoveClicked() {
        int row = tableWidget_->currentRow();
        if (row >= 0) {
            tableWidget_->removeRow(row);
            emit changed(true);
        }
    }

    void KeymapEditor::onLoadPresetClicked() {
        QString                     presetName = comboPreset_->currentText();

        QMessageBox::StandardButton reply = QMessageBox::question(
            this, _("Confirm"), _("This operation will delete all existing keys on the current keymap and replace them with the input method ") + presetName + _(". Are you sure?"),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return;
        }

        tableWidget_->setRowCount(0);

        const auto& keyList = presets_.at(presetName);

        for (const auto& pair : keyList) {
            QString key        = pair.first;
            QString actionCode = pair.second;

            int     row = tableWidget_->rowCount();
            tableWidget_->insertRow(row);
            tableWidget_->setItem(row, 0, new QTableWidgetItem(key));

            auto* cellCombo = new QComboBox();
            for (const auto& action : bambooActions_) {
                cellCombo->addItem(action.second, action.first);
            }

            int idx = cellCombo->findData(actionCode);
            if (idx >= 0) {
                cellCombo->setCurrentIndex(idx);
            }

            connect(cellCombo, &QComboBox::currentIndexChanged, this, [this]() { emit changed(true); });

            tableWidget_->setCellWidget(row, 1, cellCombo);
        }

        emit changed(true);
    }

    void KeymapEditor::onRowSelected(int row, int /*column*/) {
        if (tableWidget_->item(row, 0) == nullptr) {
            return;
        }
        inputKey_->setText(tableWidget_->item(row, 0)->text());
        auto* cellCombo = qobject_cast<QComboBox*>(tableWidget_->cellWidget(row, 1));
        if (cellCombo != nullptr) {
            comboAction_->setCurrentIndex(cellCombo->currentIndex());
        }
    }

    void KeymapEditor::onImportClicked() {
        QString path = QFileDialog::getOpenFileName(this, _("Import Keymap"), QString{}, _("Tab-separated (*.tsv *.txt);;All files (*)"));

        if (path.isEmpty()) {
            return;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, _("Error"), _("Cannot open file for reading."));
            return;
        }

        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);

        int  imported  = 0;
        int  skipped   = 0;
        bool confirmed = false; // lazy-ask once

        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue; // skip blank lines and comments
            }
            QStringList parts = line.split('\t');
            if (parts.size() < 2) {
                // Try comma as fallback
                parts = line.split(',');
            }
            if (parts.size() < 2) {
                ++skipped;
                continue;
            }
            QString key        = parts[0].trimmed();
            QString actionCode = parts[1].trimmed();
            if (key.isEmpty() || actionCode.isEmpty()) {
                ++skipped;
                continue;
            }

            // Warn once if the table already has data
            if (!confirmed && tableWidget_->rowCount() > 0) {
                auto reply = QMessageBox::question(this, _("Confirm Import"),
                                                   _("The current keymap list is not empty. Imported entries will be merged (existing keys will be updated). Continue?"),
                                                   QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::No) {
                    return;
                }
                confirmed = true;
            } else {
                confirmed = true;
            }

            // Add or update the key
            bool found = false;
            for (int i = 0; i < tableWidget_->rowCount(); ++i) {
                if (tableWidget_->item(i, 0) != nullptr && tableWidget_->item(i, 0)->text() == key) {
                    auto* cellCombo = qobject_cast<QComboBox*>(tableWidget_->cellWidget(i, 1));
                    if (cellCombo != nullptr) {
                        int idx = cellCombo->findData(actionCode);
                        if (idx >= 0) {
                            cellCombo->setCurrentIndex(idx);
                        }
                    }
                    found = true;
                    break;
                }
            }

            if (!found) {
                int row = tableWidget_->rowCount();
                tableWidget_->insertRow(row);
                tableWidget_->setItem(row, 0, new QTableWidgetItem(key));

                auto* cellCombo = new QComboBox();
                for (const auto& action : bambooActions_) {
                    cellCombo->addItem(action.second, action.first);
                }

                int idx = cellCombo->findData(actionCode);
                if (idx >= 0) {
                    cellCombo->setCurrentIndex(idx);
                }

                connect(cellCombo, &QComboBox::currentIndexChanged, this, [this]() { emit changed(true); });

                tableWidget_->setCellWidget(row, 1, cellCombo);
            }

            ++imported;
        }
        file.close();

        QMessageBox::information(this, _("Import Complete"), QString(_("Imported %1 entries, skipped %2 invalid lines.")).arg(imported).arg(skipped));
    }

    void KeymapEditor::onExportClicked() {
        if (tableWidget_->rowCount() == 0) {
            QMessageBox::information(this, _("Export"), _("The keymap list is empty, nothing to export."));
            return;
        }

        QString path = QFileDialog::getSaveFileName(this, _("Export Keymap"), "lotus-keymap.tsv", _("Tab-separated (*.tsv *.txt);;All files (*)"));

        if (path.isEmpty()) {
            return;
        }

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, _("Error"), _("Cannot open file for writing."));
            return;
        }

        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);

        out << "# Lotus Keymap Table\n";
        out << "# Format: key<TAB>action_code\n";

        for (int i = 0; i < tableWidget_->rowCount(); ++i) {
            QString key = tableWidget_->item(i, 0) != nullptr ? tableWidget_->item(i, 0)->text() : QString{};
            if (key.isEmpty()) {
                continue;
            }

            auto* cellCombo = qobject_cast<QComboBox*>(tableWidget_->cellWidget(i, 1));
            if (cellCombo == nullptr) {
                continue;
            }

            QString action = cellCombo->currentData().toString();
            out << key << '\t' << action << '\n';
        }
        file.close();

        QMessageBox::information(this, _("Export Complete"), QString(_("Exported %1 entries to:\n%2")).arg(tableWidget_->rowCount()).arg(path));
    }

    void KeymapEditor::load() {
        tableWidget_->setRowCount(0);

        lotusCustomKeymap config;
#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif
        std::string path = configDir + "/lotus-custom-keymap.conf";
        fcitx::readAsIni(config, path);

        for (const auto& item : config.customKeymap.value()) {
            QString key        = QString::fromStdString(item.key.value());
            QString actionCode = QString::fromStdString(item.value.value());

            int     row = tableWidget_->rowCount();
            tableWidget_->insertRow(row);
            tableWidget_->setItem(row, 0, new QTableWidgetItem(key));

            auto* cellCombo = new QComboBox();
            for (const auto& action : bambooActions_) {
                cellCombo->addItem(action.second, action.first);
            }

            int idx = cellCombo->findData(actionCode);
            if (idx >= 0) {
                cellCombo->setCurrentIndex(idx);
            }

            connect(cellCombo, &QComboBox::currentIndexChanged, this, [this]() { emit changed(true); });

            tableWidget_->setCellWidget(row, 1, cellCombo);
        }
        emit changed(false);
    }

    void KeymapEditor::save() {
        lotusCustomKeymap        config;
        std::vector<lotusKeymap> newList;

        for (int i = 0; i < tableWidget_->rowCount(); ++i) {
            QString key = tableWidget_->item(i, 0)->text();

            auto*   cellCombo = qobject_cast<QComboBox*>(tableWidget_->cellWidget(i, 1));
            if (cellCombo == nullptr)
                continue;

            QString     action = cellCombo->currentData().toString();

            lotusKeymap item;
            item.key.setValue(key.toStdString());
            item.value.setValue(action.toStdString());

            newList.push_back(item);
        }

        config.customKeymap.setValue(newList);
#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif
        std::string path = configDir + "/lotus-custom-keymap.conf";
        fcitx::safeSaveAsIni(config, path);

        emit changed(false);
    }
} // namespace fcitx::lotus
//NOLINTEND(cppcoreguidelines-owning-memory)