# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Keymap Editor Page. Edits lotus-custom-keymap.conf.
Implements custom keymap presets and TSV import/export.
"""

from core.dbus_handler import LotusDBusHandler
from core.keymap_presets import BAMBOO_ACTIONS, PRESETS
from core.table_editor import collect_kv_pairs, search_hides_row
from i18n import _
from qtpy.QtGui import QIcon
from qtpy.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from ui.helpers import bool_to_str, config_bool, set_add_button_mode
from ui.pages.base_editor import BaseEditorPage
from ui.pages.dynamic_settings import CardWidget


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
        self.cb_enable = QCheckBox(_("Custom Keymap"))
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

        btn_load_preset = QPushButton(QIcon.fromTheme("document-import"), _("Apply Preset"))
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
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
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
                self.cb_enable.setChecked(config_bool(values, "EnableCustomKeymap", "False"))

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
        return {
            "data": self._collect_table_rows(),
            "EnableCustomKeymap": self.cb_enable.isChecked(),
        }

    def _collect_table_rows(self):
        """Reads the table into config entries, keeping rows with widgets only."""
        rows = []
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            combo_widget = self.table.cellWidget(row, 1)
            if not key_item or not combo_widget:
                rows.append((None, None))
                continue
            rows.append((key_item.text(), combo_widget.currentData()))
        return collect_kv_pairs(rows, skip_empty_key=False)

    def save_data(self) -> bool:
        """Saves current table via DBus to C++ Engine."""
        # Save toggle
        if not self.dbus.save_values(
            {"EnableCustomKeymap": bool_to_str(self.cb_enable.isChecked())}
        ):
            return False

        if not self.dbus.set_sub_config_list(
            "custom_keymap", "CustomKeymap", self._collect_table_rows()
        ):
            return False

        self.initial_state = self._get_current_state()
        return True

    def on_search_changed(self):
        """Filters the table rows based on the search input."""
        search_text = self.search_input.text().lower()
        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, 0)
            action_item = (
                self.table.cellWidget(row, 1)
                if isinstance(self.table.cellWidget(row, 1), QComboBox)
                else self.table.item(row, 1)
            )

            key = key_item.text() if key_item else ""
            if isinstance(action_item, QComboBox):
                action = action_item.currentText()
            elif action_item:
                action = action_item.text()
            else:
                action = ""

            self.table.setRowHidden(row, search_hides_row(search_text, key, action))

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
        row = self.find_row_by_key(key)
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

    def _update_add_button_icon(self, *_args):
        """Changes the Add button icon to Update if key exists."""
        key = self.input_key.text().strip()

        # Disable button if key is empty
        self.btn_add.setEnabled(bool(key))

        found = self.find_row_by_key(key) is not None
        set_add_button_mode(
            self.btn_add, found, _("Update"), _("Add"), _("Update Keymap"), _("Add Keymap")
        )

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
