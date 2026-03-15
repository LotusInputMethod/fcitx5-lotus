/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

// NOLINTBEGIN(cppcoreguidelines-owning-memory)
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
#include <QList>
#include <QMessageBox>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <qtmetamacros.h>

namespace fcitx::lotus {

    // ─── Constructor ─────────────────────────────────────────────────────────

    MacroEditor::MacroEditor(QWidget* parent) :
        FcitxQtConfigUIWidget(parent), tableWidget_(new QTableWidget(0, 2, this)), inputKey_(new QLineEdit(this)), inputValue_(new QLineEdit(this)) {
        setMinimumWidth(600);
        setMinimumHeight(400);
        auto* mainLayout = new QHBoxLayout(this);

        // ── Left Column (Inputs + Table) ─────────────────────────────────────
        auto* leftColumn = new QVBoxLayout();

        // ── Input row ────────────────────────────────────────────────────────
        auto* inputLayout = new QHBoxLayout();
        inputKey_->setPlaceholderText(_("Abbreviation (e.g. kg)"));
        inputValue_->setPlaceholderText(_("Full text (e.g. khô gà)"));

        inputLayout->addWidget(new QLabel(_("Key:"), this));
        inputLayout->addWidget(inputKey_, 1);
        inputLayout->addWidget(new QLabel(_("Value:"), this));
        inputLayout->addWidget(inputValue_, 2);

        leftColumn->addLayout(inputLayout);

        // ── Table ────────────────────────────────────────────────────────────
        tableWidget_->setHorizontalHeaderLabels({_("Abbr."), _("Text")});
        tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget_->setAlternatingRowColors(true);

        leftColumn->addWidget(tableWidget_);
        mainLayout->addLayout(leftColumn);

        // ── Sidebar (Buttons on the right) ───────────────────────────────────
        auto* sidebarLayout = new QVBoxLayout();

        btnAdd_      = new QPushButton(QIcon::fromTheme("list-add"), _("Add"), this);
        btnRemove_   = new QPushButton(QIcon::fromTheme("list-remove"), _("Remove"), this);
        btnMoveUp_   = new QPushButton(QIcon::fromTheme("go-up"), _("Up"), this);
        btnMoveDown_ = new QPushButton(QIcon::fromTheme("go-down"), _("Down"), this);
        btnImport_   = new QPushButton(QIcon::fromTheme("document-import"), _("Import"), this);
        btnExport_   = new QPushButton(QIcon::fromTheme("document-export"), _("Export"), this);

        QString btnStyle = "QPushButton { text-align: left; padding: 6px 12px;}";
        btnAdd_->setStyleSheet(btnStyle);
        btnRemove_->setStyleSheet(btnStyle);
        btnMoveUp_->setStyleSheet(btnStyle);
        btnMoveDown_->setStyleSheet(btnStyle);
        btnImport_->setStyleSheet(btnStyle);
        btnExport_->setStyleSheet(btnStyle);

        // Use fixed size policy for buttons to prevent unwanted stretching
        btnAdd_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btnRemove_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btnMoveUp_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btnMoveDown_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btnImport_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btnExport_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        sidebarLayout->addWidget(btnAdd_);
        sidebarLayout->addWidget(btnRemove_);
        sidebarLayout->addSpacing(10);
        sidebarLayout->addWidget(btnMoveUp_);
        sidebarLayout->addWidget(btnMoveDown_);
        sidebarLayout->addStretch();
        sidebarLayout->addWidget(btnImport_);
        sidebarLayout->addWidget(btnExport_);

        mainLayout->addLayout(sidebarLayout);

        // ── Connections ──────────────────────────────────────────────────────
        connect(btnAdd_, &QPushButton::clicked, this, &MacroEditor::onAddClicked);
        connect(btnRemove_, &QPushButton::clicked, this, &MacroEditor::onRemoveClicked);
        connect(btnMoveUp_, &QPushButton::clicked, this, &MacroEditor::onMoveUpClicked);
        connect(btnMoveDown_, &QPushButton::clicked, this, &MacroEditor::onMoveDownClicked);
        connect(btnImport_, &QPushButton::clicked, this, &MacroEditor::onImportClicked);
        connect(btnExport_, &QPushButton::clicked, this, &MacroEditor::onExportClicked);
        connect(inputValue_, &QLineEdit::returnPressed, this, &MacroEditor::onAddClicked);
        connect(tableWidget_, &QTableWidget::cellClicked, this, &MacroEditor::onRowSelected);

        updateButtonStates();
    }

    // ─── Metadata ────────────────────────────────────────────────────────────

    QString MacroEditor::title() {
        return _("Lotus Macro");
    }

    QString MacroEditor::icon() {
        return "fcitx-lotus";
    }

    // ─── Helpers ─────────────────────────────────────────────────────────────

    void MacroEditor::upsertRow(const QString& key, const QString& value) {
        // Update if key already exists
        for (int i = 0; i < tableWidget_->rowCount(); ++i) {
            if (tableWidget_->item(i, 0) != nullptr && tableWidget_->item(i, 0)->text() == key) {
                tableWidget_->item(i, 1)->setText(value);
                updateButtonStates();
                emit changed(true);
                return;
            }
        }

        // Insert new row
        int row = tableWidget_->rowCount();
        tableWidget_->insertRow(row);

        tableWidget_->setItem(row, 0, new QTableWidgetItem(key));
        tableWidget_->setItem(row, 1, new QTableWidgetItem(value));

        updateButtonStates();
        emit changed(true);
    }

    void MacroEditor::updateButtonStates() {
        int row      = tableWidget_->currentRow();
        int rowCount = tableWidget_->rowCount();

        btnMoveUp_->setEnabled(row > 0);
        btnMoveDown_->setEnabled(row >= 0 && row < rowCount - 1);
    }

    // ─── Slots ───────────────────────────────────────────────────────────────

    void MacroEditor::onAddClicked() {
        QString key   = inputKey_->text().trimmed();
        QString value = inputValue_->text().trimmed();
        if (key.isEmpty() || value.isEmpty()) {
            return;
        }
        upsertRow(key, value);
        inputKey_->clear();
        inputValue_->clear();
        inputKey_->setFocus();
    }

    void MacroEditor::onRemoveClicked() {
        auto selectedRanges = tableWidget_->selectedRanges();
        if (selectedRanges.isEmpty()) {
            return;
        }

        // Collect all row indices from all selected ranges
        QSet<int> rowsToDelete;
        for (const auto& range : selectedRanges) {
            for (int i = range.topRow(); i <= range.bottomRow(); ++i) {
                rowsToDelete.insert(i);
            }
        }

        if (rowsToDelete.isEmpty()) {
            return;
        }

        // Convert to list and sort descending to prevent index shifting
        QList<int> sortedRows = rowsToDelete.values();
        std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());

        for (int row : sortedRows) {
            tableWidget_->removeRow(row);
        }

        updateButtonStates();
        emit changed(true);
    }

    void MacroEditor::onMoveUpClicked() {
        int row = tableWidget_->currentRow();
        if (row <= 0) {
            return;
        }

        auto* keyItem   = tableWidget_->takeItem(row, 0);
        auto* valueItem = tableWidget_->takeItem(row, 1);
        auto* prevKey   = tableWidget_->takeItem(row - 1, 0);
        auto* prevValue = tableWidget_->takeItem(row - 1, 1);

        tableWidget_->setItem(row - 1, 0, keyItem);
        tableWidget_->setItem(row - 1, 1, valueItem);
        tableWidget_->setItem(row, 0, prevKey);
        tableWidget_->setItem(row, 1, prevValue);

        tableWidget_->setCurrentCell(row - 1, 0);
        updateButtonStates();
        emit changed(true);
    }

    void MacroEditor::onMoveDownClicked() {
        int row = tableWidget_->currentRow();
        if (row < 0 || row >= tableWidget_->rowCount() - 1) {
            return;
        }

        auto* keyItem   = tableWidget_->takeItem(row, 0);
        auto* valueItem = tableWidget_->takeItem(row, 1);
        auto* nextKey   = tableWidget_->takeItem(row + 1, 0);
        auto* nextValue = tableWidget_->takeItem(row + 1, 1);

        tableWidget_->setItem(row + 1, 0, keyItem);
        tableWidget_->setItem(row + 1, 1, valueItem);
        tableWidget_->setItem(row, 0, nextKey);
        tableWidget_->setItem(row, 1, nextValue);

        tableWidget_->setCurrentCell(row + 1, 0);
        updateButtonStates();
        emit changed(true);
    }

    void MacroEditor::onRowSelected(int row, int /*column*/) {
        if (tableWidget_->item(row, 0) == nullptr) {
            return;
        }
        inputKey_->setText(tableWidget_->item(row, 0)->text());
        inputValue_->setText(tableWidget_->item(row, 1) != nullptr ? tableWidget_->item(row, 1)->text() : QString{});
        updateButtonStates();
    }

    void MacroEditor::onImportClicked() {
        QString path = QFileDialog::getOpenFileName(this, _("Import Macros"), QString{}, _("Tab-separated (*.tsv *.txt);;All files (*)"));

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
            QString key   = parts[0].trimmed();
            QString value = parts[1].trimmed();
            if (key.isEmpty() || value.isEmpty()) {
                ++skipped;
                continue;
            }

            // Warn once if the table already has data
            if (!confirmed && tableWidget_->rowCount() > 0) {
                auto reply = QMessageBox::question(this, _("Confirm Import"),
                                                   _("The current macro list is not empty. Imported entries will be merged (existing keys will be updated). Continue?"),
                                                   QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::No) {
                    return;
                }
                confirmed = true;
            } else {
                confirmed = true;
            }

            upsertRow(key, value);
            ++imported;
        }
        file.close();

        QMessageBox::information(this, _("Import Complete"), QString(_("Imported %1 entries, skipped %2 invalid lines.")).arg(imported).arg(skipped));
    }

    void MacroEditor::onExportClicked() {
        if (tableWidget_->rowCount() == 0) {
            QMessageBox::information(this, _("Export"), _("The macro list is empty, nothing to export."));
            return;
        }

        QString path = QFileDialog::getSaveFileName(this, _("Export Macros"), "lotus-macro.tsv", _("Tab-separated (*.tsv *.txt);;All files (*)"));

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

        out << "# Lotus Macro Table\n";
        out << "# Format: shorthand<TAB>expanded text\n";

        for (int i = 0; i < tableWidget_->rowCount(); ++i) {
            QString key   = tableWidget_->item(i, 0) != nullptr ? tableWidget_->item(i, 0)->text() : QString{};
            QString value = tableWidget_->item(i, 1) != nullptr ? tableWidget_->item(i, 1)->text() : QString{};
            if (!key.isEmpty()) {
                out << key << '\t' << value << '\n';
            }
        }
        file.close();

        QMessageBox::information(this, _("Export Complete"), QString(_("Exported %1 entries to:\n%2")).arg(tableWidget_->rowCount()).arg(path));
    }

    // ─── load / save ─────────────────────────────────────────────────────────

    void MacroEditor::load() {
        tableWidget_->setRowCount(0);

        lotusMacroTable config;
#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif
        std::string path = configDir + "/lotus-macro-table.conf";
        fcitx::readAsIni(config, path);

        for (const auto& item : config.macros.value()) {
            QString key   = QString::fromStdString(item.key.value());
            QString value = QString::fromStdString(item.value.value());
            upsertRow(key, value);
        }
        emit changed(false);
    }

    void MacroEditor::save() {
        lotusMacroTable          config;
        std::vector<lotusKeymap> newList;

        for (int i = 0; i < tableWidget_->rowCount(); ++i) {
            QString key   = tableWidget_->item(i, 0) != nullptr ? tableWidget_->item(i, 0)->text() : QString{};
            QString value = tableWidget_->item(i, 1) != nullptr ? tableWidget_->item(i, 1)->text() : QString{};
            if (key.isEmpty()) {
                continue;
            }

            lotusKeymap item;
            item.key.setValue(key.toStdString());
            item.value.setValue(value.toStdString());
            newList.push_back(item);
        }

        config.macros.setValue(newList);

#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif
        std::string path = configDir + "/lotus-macro-table.conf";
        fcitx::safeSaveAsIni(config, path);

        emit changed(false);
    }

} // namespace fcitx::lotus
// NOLINTEND(cppcoreguidelines-owning-memory)
