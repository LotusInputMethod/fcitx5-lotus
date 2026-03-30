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
    QCheckBox,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
from i18n import _
from core.dbus_handler import LotusDBusHandler
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
    "Microsoft layout": [
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

    def __init__(self, dbus_handler: LotusDBusHandler, parent=None):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.initial_state = {}
        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(30, 20, 30, 20)
        main_layout.setSpacing(15)

        title = QLabel(_("Keymap"))
        title.setObjectName("CategoryTitle")
        main_layout.addWidget(title)

        # Configuration card
        config_card = CardWidget("")
        config_layout = QVBoxLayout()
        
        # Row 1: Enable checkbox and Search
        top_row = QHBoxLayout()
        self.cb_enable = QCheckBox(_("Enable Custom Keymap"))
        self.cb_enable.toggled.connect(self._on_item_changed)
        top_row.addWidget(self.cb_enable)
        top_row.addStretch()
        
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText(_("Search keys..."))
        self.search_input.setClearButtonEnabled(True)
        self.search_input.setFixedWidth(200)
        self.search_input.textChanged.connect(self.on_search_changed)
        top_row.addWidget(QLabel(_("Search:")))
        top_row.addWidget(self.search_input)
        config_layout.addLayout(top_row)
        
        # Row 2: Original Input Method (Preset)
        bottom_row = QHBoxLayout()
        bottom_row.addWidget(QLabel(_("Original Input Method:")))
        self.combo_preset = QComboBox()
        self.combo_preset.addItems(PRESETS.keys())
        bottom_row.addWidget(self.combo_preset)
        
        btn_load_preset = QPushButton(
            QIcon.fromTheme("document-import"), _("Apply Preset")
        )
        btn_load_preset.clicked.connect(self.on_load_preset)
        bottom_row.addWidget(btn_load_preset)
        bottom_row.addStretch()
        config_layout.addLayout(bottom_row)
        
        config_card.content_layout.addLayout(config_layout)
        main_layout.addWidget(config_card)

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

        self.btn_add = QPushButton(QIcon.fromTheme("list-add"), _("Add"))
        self.btn_add.setToolTip(_("Add Keymap"))
        self.btn_add.clicked.connect(self.on_add)
        self.input_key.textChanged.connect(self._update_add_button_icon)

        input_layout.addWidget(self.input_key)
        input_layout.addWidget(self.combo_action)
        input_layout.addWidget(self.btn_add)
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

        self.btn_remove = QPushButton(QIcon.fromTheme("list-remove"), _("Remove"))
        self.btn_remove.setToolTip(_("Remove selected row"))
        self.btn_remove.clicked.connect(self.on_remove)



        toolbar_layout.addWidget(self.btn_remove)
        toolbar_layout.addStretch()

        editor_layout.addLayout(toolbar_layout)
        self.update_button_states()

    def load_data(self):
        """Loads keymap data strictly via D-Bus."""
        self.blockSignals(True)
        try:
            config_data = self.dbus.get_config()
            if config_data:
                values = config_data.get("values", {})
                self.cb_enable.setChecked(
                    str(values.get("EnableCustomKeymap", "False")).lower() == "true"
                )

            self.table.setRowCount(0)
            data = self.dbus.get_sub_config_list("custom_keymap", "CustomKeymap")
            for item in data:
                self._add_row(item.get("Key", ""), item.get("Value", ""))
            self.initial_state = self._get_current_state()
        finally:
            self.blockSignals(False)
            self.on_search_changed()

    def restore_defaults(self):
        """Clears all custom keymap entries, restoring to default."""
        self.table.setRowCount(0)
        self._on_item_changed()

    def is_modified_from_default(self):
        """Returns True if the keymap table has any entries."""
        return self.table.rowCount() > 0

    def is_modified(self):
        """Returns True if the current state differs from the initial loaded state."""
        return self._get_current_state() != self.initial_state

    def _get_current_state(self):
        """Captures the current UI state for comparison."""
        data = []
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            combo_widget = self.table.cellWidget(row, 1)
            if key_item and combo_widget:
                data.append({"Key": key_item.text(), "Value": combo_widget.currentData()})
        return {
            "data": data,
            "EnableCustomKeymap": self.cb_enable.isChecked(),
        }

    def save_data(self):
        """Saves current table via DBus to C++ Engine."""
        # Save toggle
        config_data = self.dbus.get_config()
        if config_data:
            values = config_data.get("values", {})
            values["EnableCustomKeymap"] = "True" if self.cb_enable.isChecked() else "False"
            self.dbus.set_config(values)

        data = []
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            combo_widget = self.table.cellWidget(row, 1)
            if not key_item or not combo_widget:
                continue
            data.append({"Key": key_item.text(), "Value": combo_widget.currentData()})

        self.dbus.set_sub_config_list("custom_keymap", "CustomKeymap", data)
        self.initial_state = self._get_current_state()

    def on_search_changed(self):
        """Filters the table rows based on the search input."""
        search_text = self.search_input.text().lower()
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            action_item = self.table.cellWidget(row, 1) if isinstance(self.table.cellWidget(row, 1), QComboBox) else self.table.item(row, 1)
            
            key = key_item.text().lower() if key_item else ""
            action = ""
            if isinstance(action_item, QComboBox):
                action = action_item.currentText().lower()
            elif action_item:
                action = action_item.text().lower()
                
            self.table.setRowHidden(row, search_text not in key and search_text not in action)

    def on_add(self):
        """Adds or updates a keymap entry."""
        key = self.input_key.text().strip()
        if not key:
            return

        self.upsert_row(key, self.combo_action.currentData())
        self.input_key.clear()
        self.input_key.setFocus()

    def upsert_row(self, key: str, action_code: str):
        """Adds or updates a row in the keymap table."""
        row = self._find_row_by_key(key)
        if row is not None:
            # Update existing
            cell_combo = self.table.cellWidget(row, 1)
            if cell_combo:
                idx = cell_combo.findData(action_code)
                if idx >= 0:
                    cell_combo.setCurrentIndex(idx)
            self._on_item_changed()
            return

        # Insert new
        self._add_row(key, action_code)
        self.on_search_changed()
        self.update_button_states()
        self._on_item_changed()

    def _find_row_by_key(self, key: str) -> int | None:
        """Finds row index for a given key. Returns None if not found."""
        for r in range(self.table.rowCount()):
            item = self.table.item(r, 0)
            if item and item.text() == key:
                return r
        return None

    def _update_add_button_icon(self, *_args):
        """Changes the Add button icon to Update if key exists."""
        key = self.input_key.text().strip()

        # Disable button if key is empty
        self.btn_add.setEnabled(bool(key))

        found = self._find_row_by_key(key) is not None
        if found:
            self.btn_add.setIcon(QIcon.fromTheme("document-save"))
            self.btn_add.setText(_("Update"))
            self.btn_add.setToolTip(_("Update Keymap"))
        else:
            self.btn_add.setIcon(QIcon.fromTheme("list-add"))
            self.btn_add.setText(_("Add"))
            self.btn_add.setToolTip(_("Add Keymap"))

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

    def do_import(self):
        """Imports keymap from a TSV file."""
        path, _filter = QFileDialog.getOpenFileName(
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
        except (IOError, OSError, UnicodeDecodeError) as e:
            QMessageBox.warning(self, _("Error"), _("Cannot open file for reading: {}").format(e))
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
                item = self.table.item(row, 0)
                if item and item.text() == key:
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
            _("Imported {} entries, skipped {} invalid lines.").format(imported, skipped),
        )

    def do_export(self):
        """Exports the current table to a TSV file."""
        if self.table.rowCount() == 0:
            QMessageBox.information(
                self, _("Export"), _("The keymap list is empty, nothing to export.")
            )
            return

        path, _filter = QFileDialog.getSaveFileName(
            self,
            _("Export Keymap"),
            "lotus-keymap.tsv",
            _("Tab-separated (*.tsv);;Text files (*.txt);;All files (*)"),
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
                _("Exported {} entries to:\n{}").format(self.table.rowCount(), path),
            )
        except (IOError, OSError, UnicodeDecodeError) as e:
            QMessageBox.warning(self, _("Error"), _("Cannot open file for writing: {}").format(e))
