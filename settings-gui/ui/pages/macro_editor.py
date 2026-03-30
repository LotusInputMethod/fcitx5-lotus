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
    QComboBox,
)
from PySide6.QtGui import QIcon, QColor
from PySide6.QtCore import Qt
from i18n import _
from core.dbus_handler import LotusDBusHandler
from ui.pages.base_editor import BaseEditorPage
from ui.pages.dynamic_settings import CardWidget


class MacroEditorPage(BaseEditorPage):
    """UI for editing Lotus macros."""

    def __init__(
        self,
        dbus_handler: LotusDBusHandler,
        parent=None,
    ):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.initial_state = {}
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
        
        cap_layout = QHBoxLayout()
        cap_layout.setSpacing(5)
        cap_layout.addWidget(self.cb_capitalize)
        
        help_icon = QLabel()
        help_icon.setPixmap(QIcon.fromTheme("help-about").pixmap(16, 16))
        help_icon.setToolTip(_("Automatically match expansion case to trigger key case:<br>- 'kg' → 'khô gà' (all lowercase)<br>- 'KG' → 'KHÔ GÀ' (all uppercase)<br>- 'Kg' → 'khô gà' (original macro case)"))
        cap_layout.addWidget(help_icon)
        
        toggles_layout.addWidget(self.cb_enable)
        toggles_layout.addLayout(cap_layout)
        toggles_layout.addStretch()

        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText(_("Search macros..."))
        self.search_input.setClearButtonEnabled(True)
        self.search_input.setFixedWidth(200)
        self.search_input.textChanged.connect(self.on_search_changed)
        toggles_layout.addWidget(QLabel(_("Search:")))
        toggles_layout.addWidget(self.search_input)

        toggles_card.content_layout.addLayout(toggles_layout)
        main_layout.addWidget(toggles_card)

        # Main content area
        editor_card = CardWidget("")
        content_layout = QVBoxLayout()
        editor_card.content_layout.addLayout(content_layout)
        main_layout.addWidget(editor_card)

        # Dynamic Macro Settings (Moved below the macro table)
        dynamic_card = CardWidget("")
        dynamic_layout = QVBoxLayout()
        
        # Hint text
        hint_label = QLabel(_("Macros can use dynamic placeholders: $TIME (current time) and $DATE (current date)."))
        hint_label.setWordWrap(True)
        hint_label.setStyleSheet("color: gray; font-size: 13px;")
        dynamic_layout.addWidget(hint_label)
        
        # Format Inputs
        fmt_container = QWidget()
        fmt_hbox = QHBoxLayout(fmt_container)
        fmt_hbox.setContentsMargins(0, 5, 0, 0)
        fmt_hbox.setSpacing(20)
        
        # Time Format
        time_layout = QHBoxLayout()
        self.input_time_format = QComboBox()
        self.input_time_format.setEditable(False)
        self.input_time_format.currentIndexChanged.connect(self._on_item_changed)
        
        time_presets = [
            ("%H:%M", "15:04 (24h)"),
            ("%H:%M:%S", "15:04:05 (24h)"),
            ("%I:%M %p", "03:04 PM"),
            ("%I:%M:%S %p", "03:04:05 PM"),
            ("", _("None (Do not replace $TIME)")),
        ]
        for fmt, desc in time_presets:
            self.input_time_format.addItem(fmt, fmt)
            self.input_time_format.setItemData(self.input_time_format.count() - 1, desc, Qt.ToolTipRole)

        time_layout.addWidget(QLabel(_("Time Format:")))
        time_layout.addWidget(self.input_time_format, 1)
        
        # Date Format
        date_layout = QHBoxLayout()
        self.input_date_format = QComboBox()
        self.input_date_format.setEditable(False)
        self.input_date_format.currentIndexChanged.connect(self._on_item_changed)

        date_presets = [
            ("%d/%m/%Y", "dd/MM/yyyy"),
            ("%d/%m/%y", "dd/MM/yy"),
            ("%m/%d/%Y", "MM/dd/yyyy"),
            ("%Y-%m-%d", "yyyy-MM-dd"),
            ("%y-%m-%d", "yy-MM-dd"),
            ("", _("None (Do not replace $DATE)")),
        ]
        for fmt, desc in date_presets:
            self.input_date_format.addItem(fmt, fmt)
            self.input_date_format.setItemData(self.input_date_format.count() - 1, desc, Qt.ToolTipRole)

        date_layout.addWidget(QLabel(_("Date Format:")))
        date_layout.addWidget(self.input_date_format, 1)
        
        fmt_hbox.addLayout(time_layout)
        fmt_hbox.addLayout(date_layout)
        
        dynamic_layout.addWidget(fmt_container)
        
        dynamic_card.content_layout.addLayout(dynamic_layout)
        main_layout.addWidget(dynamic_card)

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
        self.input_val.textChanged.connect(self._update_add_button_icon)

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
        self.apply_table_style()  # Apply custom table styling
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

        self.btn_remove.clicked.connect(self.on_remove)
        self.btn_up.clicked.connect(self.on_move_up)
        self.btn_down.clicked.connect(self.on_move_down)

        toolbar_layout.addWidget(self.btn_remove)
        toolbar_layout.addWidget(self.btn_up)
        toolbar_layout.addWidget(self.btn_down)
        toolbar_layout.addStretch()

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
                # Set time format (default %H:%M)
                time_fmt = values.get("TimeFormat", "%H:%M")
                index = self.input_time_format.findData(time_fmt)
                if index >= 0:
                    self.input_time_format.setCurrentIndex(index)
                else:
                    # Fallback to default if not in list (since it's not editable anymore)
                    self.input_time_format.setCurrentIndex(self.input_time_format.findData("%H:%M"))

                # Set date format
                date_fmt = values.get("DateFormat", "%d/%m/%Y")
                index = self.input_date_format.findData(date_fmt)
                if index >= 0:
                    self.input_date_format.setCurrentIndex(index)
                else:
                    self.input_date_format.setCurrentIndex(self.input_date_format.findData("%d/%m/%Y"))

            self.table.setRowCount(0)
            data = self.dbus.get_sub_config_list("lotus-macro", "Macro")
            for item in data:
                self.upsert_row(item.get("Key", ""), item.get("Value", ""), sort=False)
            self.on_search_changed()
            self.initial_state = self._get_current_state()
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

    def is_modified(self):
        """Returns True if the current state differs from the initial loaded state."""
        return self._get_current_state() != self.initial_state

    def _get_current_state(self):
        """Captures the current UI state for comparison."""
        data = []
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            val_item = self.table.item(row, 1)
            if key_item and key_item.text():
                data.append(
                    {"Key": key_item.text(), "Value": val_item.text() if val_item else ""}
                )
        return {
            "data": data,
            "EnableMacro": self.cb_enable.isChecked(),
            "CapitalizeMacro": self.cb_capitalize.isChecked(),
            "TimeFormat": self.input_time_format.currentText(),
            "DateFormat": self.input_date_format.currentText(),
        }

    def save_data(self):
        # Save global macro settings via DBus
        config_data = self.dbus.get_config()
        if config_data:
            values = config_data.get("values", {})
            values["EnableMacro"] = "True" if self.cb_enable.isChecked() else "False"
            values["CapitalizeMacro"] = (
                "True" if self.cb_capitalize.isChecked() else "False"
            )
            values["TimeFormat"] = self.input_time_format.currentText()
            values["DateFormat"] = self.input_date_format.currentText()
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

        self.dbus.set_sub_config_list("lotus-macro", "Macro", data)
        self.initial_state = self._get_current_state()

    def _find_row_by_key(self, key: str) -> int | None:
        """Finds row index for a given key. Returns None if not found."""
        for r in range(self.table.rowCount()):
            item = self.table.item(r, 0)
            if item and item.text() == key:
                return r
        return None

    def upsert_row(self, key: str, value: str, sort: bool = True):
        # Update existing
        row = self._find_row_by_key(key)
        if row is not None:
            self.table.item(row, 1).setText(value)
            self._apply_row_highlight(row, key)
            if sort:
                self.on_search_changed()  # Re-apply filter
            self.update_button_states()
            self._on_item_changed()
            return

        # Insert new
        row = self.table.rowCount()
        self.table.insertRow(row)
        self.table.setItem(row, 0, QTableWidgetItem(key))
        self.table.setItem(row, 1, QTableWidgetItem(value))
        self._apply_row_highlight(row, key)
        self.on_search_changed()
        if sort:
            self.on_search_changed()  # Re-apply filter
        self.update_button_states()
        self._on_item_changed()

    def _is_invalid_macro(self, key: str) -> bool:
        """Checks if macro key contains spaces or non-letter characters."""
        if not key:
            return False
        # Allow alphanumeric characters (including Unicode letters)
        return not key.isalnum()

    def _apply_row_highlight(self, row: int, key: str):
        """Applies red background and warning icon to rows with invalid keys."""
        is_invalid = self._is_invalid_macro(key)
        bg_color = Qt.transparent
        tooltip = ""
        icon = QIcon()
        if is_invalid:
            # Use a soft red for warning background
            bg_color = QColor(Qt.red)
            bg_color.setAlpha(60)
            icon = QIcon.fromTheme("dialog-warning")
            tooltip = _("Warning: Macro key should not contain spaces or special characters.")

        for col in range(self.table.columnCount()):
            item = self.table.item(row, col)
            if item:
                item.setBackground(bg_color)
                item.setData(Qt.ForegroundRole, None)
                item.setToolTip(tooltip)
                # Show icon in the first column
                if col == 0:
                    item.setIcon(icon)
                else:
                    item.setIcon(QIcon())

    def sort_invalid_to_top(self):
        """Moves all invalid entries to the top, then sorts by key."""
        # We'll extract all items, sort them, and put them back.
        rows = []
        for row in range(self.table.rowCount()):
            key = self.table.item(row, 0).text() if self.table.item(row, 0) else ""
            val = self.table.item(row, 1).text() if self.table.item(row, 1) else ""
            rows.append((key, val))

        rows.sort(key=lambda x: (not self._is_invalid_macro(x[0]), x[0].lower()))

        self.blockSignals(True)
        self.table.setRowCount(0)
        for key, val in rows:
            row_idx = self.table.rowCount()
            self.table.insertRow(row_idx)
            self.table.setItem(row_idx, 0, QTableWidgetItem(key))
            self.table.setItem(row_idx, 1, QTableWidgetItem(val))
            self._apply_row_highlight(row_idx, key)
        self.on_search_changed()  # Re-apply filter
        self.blockSignals(False)

    def on_search_changed(self):
        """Filters the table rows based on the search input."""
        search_text = self.search_input.text().lower()
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            val_item = self.table.item(row, 1)
            key = key_item.text().lower() if key_item else ""
            val = val_item.text().lower() if val_item else ""
            
            # Show row if either key or value matches search text
            self.table.setRowHidden(row, search_text not in key and search_text not in val)

    def on_add(self):
        key = self.input_key.text().strip()
        val = self.input_val.text().strip()
        if not key or not val:
            return

        self.upsert_row(key, val)
        self.input_key.clear()
        self.input_val.clear()
        self.input_key.setFocus()

    def _update_add_button_icon(self):
        """Changes the Add button icon to Update if key exists and handles validation."""
        key = self.input_key.text().strip()
        val = self.input_val.text().strip()
        is_invalid = self._is_invalid_macro(key)
        
        # Validation feedback for input field
        if is_invalid:
            self.input_key.setStyleSheet("color: red;")
            self.input_key.setToolTip(_("Warning: Macro key should not contain spaces or special characters."))
        else:
            self.input_key.setStyleSheet("")
            self.input_key.setToolTip("")

        # Disable button if key is invalid, empty, or value is empty
        self.btn_add.setEnabled(not is_invalid and bool(key) and bool(val))

        found = self._find_row_by_key(key) is not None
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

    def do_import(self):
        path, _filter = QFileDialog.getOpenFileName(
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
            _("Imported {} entries, skipped {} invalid lines.").format(imported, skipped),
        )

    def do_export(self):
        if self.table.rowCount() == 0:
            QMessageBox.information(
                self, _("Export"), _("The macro list is empty, nothing to export.")
            )
            return
        path, _filter = QFileDialog.getSaveFileName(
            self,
            _("Export Macros"),
            "lotus-macro.tsv",
            _("Tab-separated (*.tsv);;Text files (*.txt);;All files (*)"),
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
                _("Exported {} entries to:\n{}").format(self.table.rowCount(), path),
            )
        except (IOError, OSError, UnicodeDecodeError) as e:
            QMessageBox.warning(self, _("Error"), _("Cannot open file for writing: {}").format(e))
