/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef _LOTUS_MACRO_EDITOR_EDITOR_H_
#define _LOTUS_MACRO_EDITOR_EDITOR_H_

#include <fcitxqtconfiguiwidget.h>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace fcitx::lotus {

    /**
     * @brief Lotus macro editor.
     *
     * Allows editing, importing and exporting the macro table for the Lotus
     * input method using a Qt GUI. The table has two columns:
     *   - Abbreviation (the trigger key sequence)
     *   - Text (the expanded output)
     */
    class MacroEditor : public FcitxQtConfigUIWidget {
        Q_OBJECT
      public:
        /**
         * @brief Constructs a macro editor.
         * @param parent Parent widget.
         */
        explicit MacroEditor(QWidget* parent = nullptr);

        /**
         * @brief Returns the title of the macro editor.
         */
        QString title() override;

        /**
         * @brief Returns the icon of the macro editor.
         */
        QString icon() override;

        /**
         * @brief Loads the macro table from the configuration file.
         */
        void load() override;

        /**
         * @brief Saves the macro table to the configuration file.
         */
        void save() override;

      private Q_SLOTS:
        /**
         * @brief Adds or updates an entry from the input fields.
         */
        void onAddClicked();

        /**
         * @brief Removes the currently selected row.
         */
        void onRemoveClicked();

        /**
         * @brief Imports a macro list from a TSV/CSV file.
         */
        void onImportClicked();

        /**
         * @brief Exports the macro list to a TSV/CSV file.
         */
        void onExportClicked();

        /**
         * @brief Moves the currently selected row up.
         */
        void onMoveUpClicked();

        /**
         * @brief Moves the currently selected row down.
         */
        void onMoveDownClicked();

        /**
         * @brief Populates the input fields when a row is selected.
         */
        void onRowSelected(int row, int column);

      private: //NOLINT
        /**
         * @brief Inserts or updates a row in the table.
         * @param key Abbreviation (trigger key sequence).
         * @param value Expanded output text.
         */
        void upsertRow(const QString& key, const QString& value);
        /**
         * @brief Updates the enabled state of Move Up/Down buttons based on current selection.
         */
        void          updateButtonStates();

        QTableWidget* tableWidget_;
        QLineEdit*    inputKey_;
        QLineEdit*    inputValue_;
        QPushButton*  btnAdd_;
        QPushButton*  btnRemove_;
        QPushButton*  btnMoveUp_;
        QPushButton*  btnMoveDown_;
        QPushButton*  btnImport_;
        QPushButton*  btnExport_;
    };
} // namespace fcitx::lotus

#endif
