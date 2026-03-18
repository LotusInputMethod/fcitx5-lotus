# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Keymap Editor Page. Edits lotus-custom-keymap.conf.
Implements custom keymap presets and TSV import/export.
"""

import os
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QHeaderView,
    QLineEdit,
    QMessageBox,
    QComboBox,
    QLabel,
    QFrame,
    QFileDialog,
    QAbstractItemView,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
from i18n import _
from core.file_handler import Fcitx5ConfigHandler
from ui.pages.base_editor import BaseEditorPage
from ui.pages.dynamic_settings import CardWidget

BAMBOO_ACTIONS = [
    ("XoaDauThanh", "Xóa dấu thanh"),
    ("DauSac", "Dấu sắc"),
    ("DauHuyen", "Dấu huyền"),
    ("DauHoi", "Dấu hỏi"),
    ("DauNga", "Dấu ngã"),
    ("DauNang", "Dấu nặng"),
    ("A_Â", "a -> â"),
    ("E_Ê", "e -> ê"),
    ("O_Ô", "o -> ô"),
    ("AEO_ÂÊÔ", "a/e/o -> â/ê/ô"),
    ("UOA_ƯƠĂ", "u/o/a -> ư/ơ/ă"),
    ("D_Đ", "d -> đ"),
    ("UO_ƯƠ", "u/o -> ư/ơ"),
    ("A_Ă", "a -> ă"),
    ("__ă", "ă"),
    ("_Ă", "Ă"),
    ("__â", "â"),
    ("_Â", "Â"),
    ("__ê", "ê"),
    ("_Ê", "Ê"),
    ("__ô", "ô"),
    ("_Ô", "Ô"),
    ("__ư", "ư"),
    ("_Ư", "Ư"),
    ("__ơ", "ơ"),
    ("_Ơ", "Ơ"),
    ("__đ", "đ"),
    ("_Đ", "Đ"),
    ("UOA_ƯƠĂ__Ư", "u/o/a -> ư/ơ/ă, ư"),
]

PRESETS = {
    "Telex": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ"),
        ("d", "D_Đ"),
    ],
    "VNI": [
        ("0", "XoaDauThanh"),
        ("1", "DauSac"),
        ("2", "DauHuyen"),
        ("3", "DauHoi"),
        ("4", "DauNga"),
        ("5", "DauNang"),
        ("6", "AEO_ÂÊÔ"),
        ("7", "UO_ƯƠ"),
        ("8", "A_Ă"),
        ("9", "D_Đ"),
    ],
    "VIQR": [
        ("0", "XoaDauThanh"),
        ("'", "DauSac"),
        ("`", "DauHuyen"),
        ("?", "DauHoi"),
        ("~", "DauNga"),
        (".", "DauNang"),
        ("^", "AEO_ÂÊÔ"),
        ("+", "UO_ƯƠ"),
        ("*", "UO_ƯƠ"),
        ("(", "A_Ă"),
        ("d", "D_Đ"),
    ],
    "Microslop layout": [
        ("8", "DauSac"),
        ("5", "DauHuyen"),
        ("6", "DauHoi"),
        ("7", "DauNga"),
        ("9", "DauNang"),
        ("1", "__ă"),
        ("!", "_Ă"),
        ("2", "__â"),
        ("@", "_Â"),
        ("3", "__ê"),
        ("#", "_Ê"),
        ("4", "__ô"),
        ("$", "_Ô"),
        ("0", "__đ"),
        (")", "_Đ"),
        ("[", "__ư"),
        ("{", "_Ư"),
        ("]", "__ơ"),
        ("}", "_Ơ"),
    ],
    "Telex 2": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ__Ư"),
        ("d", "D_Đ"),
        ("]", "__ư"),
        ("[", "__ơ"),
        ("}", "_Ư"),
        ("{", "_Ơ"),
    ],
    "Telex + VNI": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ"),
        ("d", "D_Đ"),
        ("0", "XoaDauThanh"),
        ("1", "DauSac"),
        ("2", "DauHuyen"),
        ("3", "DauHoi"),
        ("4", "DauNga"),
        ("5", "DauNang"),
        ("6", "AEO_ÂÊÔ"),
        ("7", "UO_ƯƠ"),
        ("8", "A_Ă"),
        ("9", "D_Đ"),
    ],
    "Telex + VNI + VIQR": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ"),
        ("d", "D_Đ"),
        ("0", "XoaDauThanh"),
        ("1", "DauSac"),
        ("2", "DauHuyen"),
        ("3", "DauHoi"),
        ("4", "DauNga"),
        ("5", "DauNang"),
        ("6", "AEO_ÂÊÔ"),
        ("7", "UO_ƯƠ"),
        ("8", "A_Ă"),
        ("9", "D_Đ"),
        ("'", "DauSac"),
        ("`", "DauHuyen"),
        ("?", "DauHoi"),
        ("~", "DauNga"),
        (".", "DauNang"),
        ("^", "AEO_ÂÊÔ"),
        ("+", "UO_ƯƠ"),
        ("*", "UO_ƯƠ"),
        ("(", "A_Ă"),
        ("\\\\", "D_Đ"),
    ],
    "VNI Bàn phím tiếng Pháp": [
        ("&", "XoaDauThanh"),
        ("é", "DauSac"),
        ('"', "DauHuyen"),
        ("'", "DauHoi"),
        ("(", "DauNga"),
        ("-", "DauNang"),
        ("è", "AEO_ÂÊÔ"),
        ("_", "UO_ƯƠ"),
        ("ç", "A_Ă"),
        ("à", "D_Đ"),
    ],
    "Telex W": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ__Ư"),
        ("d", "D_Đ"),
    ],
}


class KeymapEditorPage(BaseEditorPage):
    """UI for editing Lotus custom keymap."""

    def __init__(self, config_handler: Fcitx5ConfigHandler, parent=None):
        super().__init__(parent)
        self.handler = config_handler
        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(30, 20, 30, 20)
        main_layout.setSpacing(15)

        title = QLabel(_("Keymap"))
        title.setObjectName("CategoryTitle")
        main_layout.addWidget(title)

        # Preset card
        preset_card = CardWidget("")
        preset_layout = QHBoxLayout()
        preset_layout.addWidget(QLabel(_("Original Input Method:")))

        self.combo_preset = QComboBox()
        self.combo_preset.addItems(PRESETS.keys())
        preset_layout.addWidget(self.combo_preset)

        btn_load_preset = QPushButton(
            QIcon.fromTheme("document-import"), _("Apply Preset")
        )
        btn_load_preset.clicked.connect(self.on_load_preset)
        preset_layout.addWidget(btn_load_preset)
        preset_layout.addStretch()
        preset_card.content_layout.addLayout(preset_layout)
        main_layout.addWidget(preset_card)

        # Editor card
        editor_card = CardWidget("")
        editor_layout = QVBoxLayout()
        editor_card.content_layout.addLayout(editor_layout)
        main_layout.addWidget(editor_card)

        # Input Area
        input_layout = QHBoxLayout()
        self.input_key = QLineEdit()
        self.input_key.setPlaceholderText(_("Key (Example: s)"))
        self.input_key.setMaxLength(1)
        self.input_key.setClearButtonEnabled(True)

        self.combo_action = QComboBox()
        for action_code, action_name in BAMBOO_ACTIONS:
            self.combo_action.addItem(action_name, action_code)

        btn_add = QPushButton(QIcon.fromTheme("list-add"), _("Add"))
        btn_add.setToolTip(_("Add / Update Keymap"))
        btn_add.clicked.connect(self.on_add)

        input_layout.addWidget(self.input_key)
        input_layout.addWidget(self.combo_action)
        input_layout.addWidget(btn_add)
        editor_layout.addLayout(input_layout)

        # Table
        self.table = QTableWidget(0, 2)
        self.table.setHorizontalHeaderLabels([_("Key"), _("Action")])
        self.table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.apply_table_style()
        self.table.cellClicked.connect(self.on_row_selected)
        editor_layout.addWidget(self.table)

        # 3. Bottom Toolbar Layout
        toolbar_layout = QHBoxLayout()
        toolbar_layout.setContentsMargins(0, 5, 0, 0)

        btn_remove = QPushButton(QIcon.fromTheme("list-remove"), _("Remove"))
        btn_remove.setToolTip(_("Remove selected row"))
        btn_remove.clicked.connect(self.on_remove)

        btn_import = QPushButton(QIcon.fromTheme("document-import"), _("Import"))
        btn_export = QPushButton(QIcon.fromTheme("document-export"), _("Export"))
        btn_import.clicked.connect(self.on_import)
        btn_export.clicked.connect(self.on_export)

        toolbar_layout.addWidget(btn_remove)
        toolbar_layout.addStretch()
        toolbar_layout.addWidget(btn_import)
        toolbar_layout.addWidget(btn_export)

        editor_layout.addLayout(toolbar_layout)

    def load_data(self):
        """Loads data from the INI file."""
        self.blockSignals(True)
        try:
            self.table.setRowCount(0)
            data = self.handler.read_array_config(self.handler.keymap_file, "CustomKeymap")
            for item in data:
                self._add_row(item.get("Key", ""), item.get("Value", ""))
        finally:
            self.blockSignals(False)

    def restore_defaults(self):
        """Clears all custom keymap entries, restoring to default."""
        self.table.setRowCount(0)
        self._on_item_changed()

    def is_modified_from_default(self):
        """Returns True if the keymap table has any entries."""
        return self.table.rowCount() > 0

    def save_data(self, quiet=False):
        """Saves current table to the INI file."""
        data = []
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            combo_widget = self.table.cellWidget(row, 1)

            if not key_item or not combo_widget:
                continue
            data.append({"Key": key_item.text(), "Value": combo_widget.currentData()})
        self.handler.write_array_config(self.handler.keymap_file, "CustomKeymap", data)
        if not quiet:
            QMessageBox.information(self, _("Success"), _("Keymap saved successfully."))

    def on_add(self):
        """Adds a new keymap entry."""
        key = self.input_key.text().strip()
        if not key:
            return

        # Check for update
        for row in range(self.table.rowCount()):
            if self.table.item(row, 0).text() == key:
                combo = self.table.cellWidget(row, 1)
                if combo:
                    combo.setCurrentIndex(self.combo_action.currentIndex())
                return

        self._add_row(key, self.combo_action.currentData())
        self._on_item_changed()

    def on_load_preset(self):
        """Loads a predefined set of keymaps."""
        preset_name = self.combo_preset.currentText()
        reply = QMessageBox.question(
            self,
            _("Confirm"),
            _("This operation will replace all existing keys with ")
            + preset_name
            + _(". Are you sure?"),
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply == QMessageBox.No:
            return

        self.table.setRowCount(0)
        for key, action_code in PRESETS.get(preset_name, []):
            self._add_row(key, action_code)
        self._on_item_changed()

    def _add_row(self, key: str, action_code: str):
        """Helper to insert a row and properly set the combobox."""
        row = self.table.rowCount()
        self.table.insertRow(row)
        self.table.setItem(row, 0, QTableWidgetItem(key))
        cell_combo = QComboBox()
        for code, name in BAMBOO_ACTIONS:
            cell_combo.addItem(name, code)

        idx = cell_combo.findData(action_code)
        if idx >= 0:
            cell_combo.setCurrentIndex(idx)

        cell_combo.currentIndexChanged.connect(self._on_item_changed)
        self.table.setCellWidget(row, 1, cell_combo)

    def on_row_selected(self, row, column):
        """Syncs the selected row data to the input fields."""
        key_item = self.table.item(row, 0)
        if key_item:
            self.input_key.setText(key_item.text())

        cell_combo = self.table.cellWidget(row, 1)
        if cell_combo:
            self.combo_action.setCurrentIndex(cell_combo.currentIndex())

    def on_import(self):
        """Imports keymap from a TSV file."""
        path, _ = QFileDialog.getOpenFileName(
            self,
            _("Import Keymap"),
            "",
            _("Tab-separated (*.tsv *.txt);;All files (*)"),
        )
        if not path:
            return

        try:
            with open(path, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except Exception as e:
            QMessageBox.warning(self, "Error", f"Cannot open file for reading: {e}")
            return
        imported = skipped = 0
        confirmed = False
        for line in lines:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t") if "\t" in line else line.split(",")
            if len(parts) < 2:
                skipped += 1
                continue
            key, action_code = parts[0].strip(), parts[1].strip()
            if not key or not action_code:
                skipped += 1
                continue
            if not confirmed and self.table.rowCount() > 0:
                if (
                    QMessageBox.question(
                        self,
                        _("Confirm Import"),
                        _("Merge imported entries?"),
                        QMessageBox.Yes | QMessageBox.No,
                    )
                    == QMessageBox.No
                ):
                    return
                confirmed = True
            else:
                confirmed = True

            # Upsert
            found = False
            for row in range(self.table.rowCount()):
                if self.table.item(row, 0).text() == key:
                    combo = self.table.cellWidget(row, 1)
                    if combo:
                        idx = combo.findData(action_code)
                        if idx >= 0:
                            combo.setCurrentIndex(idx)
                    found = True
                    break

            if not found:
                self._add_row(key, action_code)

            imported += 1

        QMessageBox.information(
            self,
            _("Import Complete"),
            _(f"Imported {imported} entries, skipped {skipped} invalid lines."),
        )

    def on_export(self):
        """Exports the current table to a TSV file."""
        if self.table.rowCount() == 0:
            QMessageBox.information(
                self, _("Export"), _("The keymap list is empty, nothing to export.")
            )
            return

        path, _ = QFileDialog.getSaveFileName(
            self,
            _("Export Keymap"),
            "lotus-keymap.tsv",
            _("Tab-separated (*.tsv *.txt);;All files (*)"),
        )
        if not path:
            return

        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write("# Lotus Keymap Table\n")
                f.write("# Format: key<TAB>action_code\n")

                for row in range(self.table.rowCount()):
                    key_item = self.table.item(row, 0)
                    combo = self.table.cellWidget(row, 1)
                    if key_item and combo:
                        f.write(f"{key_item.text()}\t{combo.currentData()}\n")
            QMessageBox.information(
                self,
                _("Export Complete"),
                _(f"Exported {self.table.rowCount()} entries to:\n{path}"),
            )
        except Exception as e:
            QMessageBox.warning(self, "Error", f"Cannot open file for writing: {e}")
