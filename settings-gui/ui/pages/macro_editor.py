# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Macro Editor Page. Edits lotus-macro-table.conf.
Implements UI with row reordering and TSV import/export.
"""

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
    QLabel,
    QSizePolicy,
    QAbstractItemView,
    QFileDialog,
    QCheckBox,
)
from PySide6.QtGui import QIcon
from PySide6.QtCore import Qt
from i18n import _
from core.file_handler import Fcitx5ConfigHandler
from core.dbus_handler import LotusDBusHandler
from ui.pages.base_editor import BaseEditorPage
from ui.pages.dynamic_settings import CardWidget


class MacroEditorPage(BaseEditorPage):
    """UI for editing Lotus macros."""

    def __init__(
        self,
        config_handler: Fcitx5ConfigHandler,
        dbus_handler: LotusDBusHandler,
        parent=None,
    ):
        super().__init__(parent)
        self.handler = config_handler
        self.dbus = dbus_handler
        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(30, 20, 30, 20)
        main_layout.setSpacing(15)

        title = QLabel(_("Macros"))
        title.setObjectName("CategoryTitle")
        main_layout.addWidget(title)

        # Macro behavior toggles
        toggles_card = CardWidget("")
        toggles_layout = QHBoxLayout()
        self.cb_enable = QCheckBox(_("Enable Macro"))
        self.cb_capitalize = QCheckBox(_("Capitalize Macro"))
        self.cb_enable.toggled.connect(self._on_item_changed)
        self.cb_capitalize.toggled.connect(self._on_item_changed)
        toggles_layout.addWidget(self.cb_enable)
        toggles_layout.addWidget(self.cb_capitalize)
        toggles_layout.addStretch()
        toggles_card.content_layout.addLayout(toggles_layout)
        main_layout.addWidget(toggles_card)

        # Main content area
        editor_card = CardWidget("")
        content_layout = QVBoxLayout()
        editor_card.content_layout.addLayout(content_layout)
        main_layout.addWidget(editor_card)

        # 1. Input Row (Top)
        input_layout = QHBoxLayout()
        self.input_key = QLineEdit()
        self.input_key.setPlaceholderText(_("Abbreviation (e.g. kg)"))
        self.input_key.setClearButtonEnabled(True)

        self.input_val = QLineEdit()
        self.input_val.setPlaceholderText(_("Full text (e.g. khô gà)"))
        self.input_val.setClearButtonEnabled(True)
        self.input_val.returnPressed.connect(self.on_add)

        self.btn_add = QPushButton(QIcon.fromTheme("list-add"), _("Add"))
        self.btn_add.clicked.connect(self.on_add)
        self.input_key.textChanged.connect(self._update_add_button_icon)

        input_layout.addWidget(QLabel(_("Key:")))
        input_layout.addWidget(self.input_key, 1)
        input_layout.addWidget(QLabel(_("Value:")))
        input_layout.addWidget(self.input_val, 2)
        input_layout.addWidget(self.btn_add)
        content_layout.addLayout(input_layout)

        # 2. Table Area
        self.table = QTableWidget(0, 2)
        self.table.setHorizontalHeaderLabels([_("Abbreviation"), _("Expanded Text")])
        self.table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.apply_table_style()  # Bơm CSS xịn vào đây
        self.table.cellClicked.connect(self.on_row_selected)
        content_layout.addWidget(self.table)

        # 3. Bottom Toolbar
        toolbar_layout = QHBoxLayout()
        toolbar_layout.setContentsMargins(0, 5, 0, 0)

        self.btn_remove = QPushButton(QIcon.fromTheme("list-remove"), _("Remove"))
        self.btn_up = QPushButton(QIcon.fromTheme("go-up"), "")
        self.btn_up.setToolTip(_("Move Up"))
        self.btn_down = QPushButton(QIcon.fromTheme("go-down"), "")
        self.btn_down.setToolTip(_("Move Down"))

        self.btn_import = QPushButton(QIcon.fromTheme("document-import"), _("Import"))
        self.btn_export = QPushButton(QIcon.fromTheme("document-export"), _("Export"))

        self.btn_remove.clicked.connect(self.on_remove)
        self.btn_up.clicked.connect(self.on_move_up)
        self.btn_down.clicked.connect(self.on_move_down)
        self.btn_import.clicked.connect(self.on_import)
        self.btn_export.clicked.connect(self.on_export)

        toolbar_layout.addWidget(self.btn_remove)
        toolbar_layout.addWidget(self.btn_up)
        toolbar_layout.addWidget(self.btn_down)
        toolbar_layout.addStretch()
        toolbar_layout.addWidget(self.btn_import)
        toolbar_layout.addWidget(self.btn_export)

        content_layout.addLayout(toolbar_layout)
        self.update_button_states()

    def load_data(self):
        self.blockSignals(True)
        try:
            # Load global macro settings via DBus
            config_data = self.dbus.get_config()
            if config_data:
                values = config_data.get("values", {})
                self.cb_enable.setChecked(
                    str(values.get("EnableMacro", "True")).lower() == "true"
                )
                self.cb_capitalize.setChecked(
                    str(values.get("CapitalizeMacro", "True")).lower() == "true"
                )

            self.table.setRowCount(0)
            data = self.handler.read_array_config(self.handler.macro_file, "Macro")
            for item in data:
                self.upsert_row(item.get("Key", ""), item.get("Value", ""))
        finally:
            self.blockSignals(False)

    def restore_defaults(self):
        """Resets macros to default (empty table, enabled checkboxes)."""
        self.blockSignals(True)
        try:
            self.cb_enable.setChecked(True)
            self.cb_capitalize.setChecked(True)
            self.table.setRowCount(0)
            self._on_item_changed()
        finally:
            self.blockSignals(False)

    def is_modified_from_default(self):
        """Returns True if the macro table has entries or checkboxes are changed from default."""
        # Default state: table is empty, both checkboxes are True.
        return (
            self.table.rowCount() > 0
            or not self.cb_enable.isChecked()
            or not self.cb_capitalize.isChecked()
        )

    def save_data(self, quiet=False):
        # Save global macro settings via DBus
        config_data = self.dbus.get_config()
        if config_data:
            values = config_data.get("values", {})
            values["EnableMacro"] = "True" if self.cb_enable.isChecked() else "False"
            values["CapitalizeMacro"] = (
                "True" if self.cb_capitalize.isChecked() else "False"
            )
            self.dbus.set_config(values)

        data = []
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            val_item = self.table.item(row, 1)
            if not key_item or not key_item.text():
                continue
            data.append(
                {"Key": key_item.text(), "Value": val_item.text() if val_item else ""}
            )

        self.handler.write_array_config(self.handler.macro_file, "Macro", data)
        if not quiet:
            QMessageBox.information(self, _("Success"), _("Macros saved successfully."))

    def upsert_row(self, key: str, value: str):
        # Update existing
        for row in range(self.table.rowCount()):
            if self.table.item(row, 0) and self.table.item(row, 0).text() == key:
                self.table.item(row, 1).setText(value)
                self.update_button_states()
                return

        # Insert new
        row = self.table.rowCount()
        self.table.insertRow(row)
        self.table.setItem(row, 0, QTableWidgetItem(key))
        self.table.setItem(row, 1, QTableWidgetItem(value))
        self.update_button_states()
        self._on_item_changed()

    def on_add(self):
        key = self.input_key.text().strip()
        val = self.input_val.text().strip()
        if not key or not val:
            return

        self.upsert_row(key, val)
        self.input_key.clear()
        self.input_val.clear()
        self._update_add_button_icon()
        self.input_key.setFocus()

    def _update_add_button_icon(self):
        """Changes the Add button icon to Update if key exists."""
        key = self.input_key.text().strip()
        found = any(
            self.table.item(r, 0) and self.table.item(r, 0).text() == key
            for r in range(self.table.rowCount())
        )
        if found:
            self.btn_add.setIcon(QIcon.fromTheme("document-save"))
            self.btn_add.setText(_("Update"))
        else:
            self.btn_add.setIcon(QIcon.fromTheme("list-add"))
            self.btn_add.setText(_("Add"))

    def on_row_selected(self, row, column):
        key_item = self.table.item(row, 0)
        if key_item:
            self.input_key.setText(key_item.text())
        val_item = self.table.item(row, 1)
        if val_item:
            self.input_val.setText(val_item.text())
        self.update_button_states()

    def on_import(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            _("Import Macros"),
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
            key, val = parts[0].strip(), parts[1].strip()
            if not key or not val:
                skipped += 1
                continue

            if not confirmed and self.table.rowCount() > 0:
                reply = QMessageBox.question(
                    self,
                    _("Confirm Import"),
                    _(
                        "The current macro list is not empty. Imported entries will be merged. Continue?"
                    ),
                    QMessageBox.Yes | QMessageBox.No,
                )
                if reply == QMessageBox.No:
                    return
                confirmed = True
            else:
                confirmed = True

            self.upsert_row(key, val)
            imported += 1

        QMessageBox.information(
            self,
            _("Import Complete"),
            _(f"Imported {imported} entries, skipped {skipped} invalid lines."),
        )

    def on_export(self):
        if self.table.rowCount() == 0:
            QMessageBox.information(
                self, _("Export"), _("The macro list is empty, nothing to export.")
            )
            return
        path, _ = QFileDialog.getSaveFileName(
            self,
            _("Export Macros"),
            "lotus-macro.tsv",
            _("Tab-separated (*.tsv *.txt);;All files (*)"),
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write("# Lotus Macro Table\n# Format: shorthand<TAB>expanded text\n")
                for row in range(self.table.rowCount()):
                    key_item = self.table.item(row, 0)
                    val_item = self.table.item(row, 1)
                    if key_item and val_item and key_item.text():
                        f.write(f"{key_item.text()}\t{val_item.text()}\n")
            QMessageBox.information(
                self,
                _("Export Complete"),
                _(f"Exported {self.table.rowCount()} entries to:\n{path}"),
            )
        except Exception as e:
            QMessageBox.warning(self, "Error", f"Cannot open file for writing: {e}")
