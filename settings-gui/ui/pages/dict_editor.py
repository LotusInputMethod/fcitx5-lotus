# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Dictionary Editor Page. Edits lotus-dict-table.conf.
Implements UI with row reordering and TSV import/export.
"""

import os
from pathlib import Path
from PySide6.QtWidgets import (
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QHeaderView,
    QLineEdit,
    QMessageBox,
    QLabel,
    QAbstractItemView,
    QFileDialog,
    QCheckBox,
)
from PySide6.QtGui import QIcon, QColor
from PySide6.QtCore import Qt
from i18n import _
from core.dbus_handler import LotusDBusHandler
from ui.pages.base_editor import BaseEditorPage
from ui.pages.dynamic_settings import CardWidget


class DictEditorPage(BaseEditorPage):
    """UI for editing Lotus dictionary."""

    def __init__(
        self,
        dbus_handler: LotusDBusHandler,
        parent=None,
    ):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.words = []  # List of all words
        self.initial_state = {}
        self._setup_ui()
        self.load_data()

    def _get_local_dict_path(self) -> str:
        xdg_data_home = os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share"))
        return os.path.join(xdg_data_home, "fcitx5/lotus/vietnamese.cm.dict")

    def _get_global_dict_path(self) -> str:
        # Common locations for fcitx5 pkgdata
        paths = [
            "/usr/share/fcitx5/lotus/vietnamese.cm.dict",
            "/usr/local/share/fcitx5/lotus/vietnamese.cm.dict",
        ]
        for p in paths:
            if os.path.exists(p):
                return p
        return paths[0]

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(30, 20, 30, 20)
        main_layout.setSpacing(15)

        title = QLabel(_("Custom Dictionary"))
        title.setObjectName("CategoryTitle")
        main_layout.addWidget(title)
        
        explanation = QLabel(_("Words in the custom dictionary will be protected from 'Auto Restore'. Use this for names, technical terms, or words not yet in the standard dictionary."))
        explanation.setWordWrap(True)
        explanation.setStyleSheet("color: gray; font-size: 13px;")
        main_layout.addWidget(explanation)

        # Dictionary behavior toggles
        toggles_card = CardWidget("")
        toggles_layout = QHBoxLayout()
        self.cb_enable = QCheckBox(_("Enable Custom Dictionary"))
        self.cb_enable.toggled.connect(self._on_item_changed)
        toggles_layout.addWidget(self.cb_enable)
        toggles_layout.addStretch()

        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText(_("Search words..."))
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

        # 1. Input Row (Top)
        input_layout = QHBoxLayout()
        self.input_word = QLineEdit()
        self.input_word.setPlaceholderText(_("Word (e.g. khongdau)"))
        self.input_word.setClearButtonEnabled(True)
        self.input_word.returnPressed.connect(self.on_add)

        self.btn_add = QPushButton(QIcon.fromTheme("list-add"), _("Add"))
        self.btn_add.clicked.connect(self.on_add)
        self.input_word.textChanged.connect(self._update_add_button_icon)

        input_layout.addWidget(QLabel(_("Word:")))
        input_layout.addWidget(self.input_word, 1)
        input_layout.addWidget(self.btn_add)
        content_layout.addLayout(input_layout)

        # 2. Table Area
        self.table = QTableWidget(0, 3)
        self.table.horizontalHeader().setVisible(False)
        self.table.verticalHeader().setVisible(False)
        for i in range(3):
            self.table.horizontalHeader().setSectionResizeMode(i, QHeaderView.Stretch)
        self.table.setSelectionBehavior(QAbstractItemView.SelectItems)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.apply_table_style()  # Apply custom table styling
        self.table.cellClicked.connect(self.on_cell_clicked)
        content_layout.addWidget(self.table)

        # 3. Bottom Toolbar
        toolbar_layout = QHBoxLayout()
        toolbar_layout.setContentsMargins(0, 5, 0, 0)

        self.btn_remove = QPushButton(QIcon.fromTheme("list-remove"), _("Remove"))
        self.btn_remove.clicked.connect(self.on_remove)

        toolbar_layout.addWidget(self.btn_remove)
        toolbar_layout.addStretch()

        content_layout.addLayout(toolbar_layout)
        self.update_button_states()

    def load_data(self):
        self.blockSignals(True)
        try:
            # Load global dictionary settings via DBus
            config_data = self.dbus.get_config()
            if config_data:
                values = config_data.get("values", {})
                self.cb_enable.setChecked(
                    str(values.get("EnableDictionary", "True")).lower() == "true"
                )

            self.words = []
            local_path = self._get_local_dict_path()
            global_path = self._get_global_dict_path()
            path_to_read = local_path if os.path.exists(local_path) else global_path
            
            if os.path.exists(path_to_read):
                try:
                    with open(path_to_read, "r", encoding="utf-8") as f:
                        for line in f:
                            word = line.strip()
                            if word and not word.startswith("#"):
                                self.words.append(word)
                except Exception as e:
                    print(f"Failed to read dictionary {path_to_read}: {e}")

            self._rebuild_table()
            self.initial_state = self._get_current_state()
        finally:
            self.blockSignals(False)
            self.on_search_changed()
            self.update_button_states()

    def _rebuild_table(self, filtered_words: list = None):
        """Rebuilds the table based on self.words or filtered_words."""
        display_words = filtered_words if filtered_words is not None else self.words
        
        num_cols = 3
        num_rows = (len(display_words) + num_cols - 1) // num_cols
        self.table.setRowCount(num_rows)
        
        for i, word in enumerate(display_words):
            row = i // num_cols
            col = i % num_cols
            item = QTableWidgetItem(word)
            self.table.setItem(row, col, item)
            self._apply_cell_highlight(item, word)
            
        # Clear remaining cells in the last row
        for i in range(len(display_words), num_rows * num_cols):
            row = i // num_cols
            col = i % num_cols
            self.table.setItem(row, col, QTableWidgetItem(""))

    def restore_defaults(self):
        """Resets dictionary to default."""
        self.blockSignals(True)
        try:
            self.cb_enable.setChecked(True)
            self.words = []
            self.load_data()
            self._on_item_changed()
        finally:
            self.blockSignals(False)

    def is_modified_from_default(self):
        """Returns True if the dictionary has entries or checkboxes are changed from default."""
        return len(self.words) > 0 or not self.cb_enable.isChecked()

    def is_modified(self):
        """Returns True if the current state differs from the initial loaded state."""
        return self._get_current_state() != self.initial_state

    def _get_current_state(self):
        """Captures the current UI state for comparison."""
        return {
            "words": sorted(self.words),
            "EnableDictionary": self.cb_enable.isChecked(),
        }

    def save_data(self):
        # Save global dictionary settings via DBus
        config_data = self.dbus.get_config()
        if config_data:
            values = config_data.get("values", {})
            values["EnableDictionary"] = "True" if self.cb_enable.isChecked() else "False"
            self.dbus.set_config(values)

        local_path = self._get_local_dict_path()
        try:
            os.makedirs(os.path.dirname(local_path), exist_ok=True)
            with open(local_path, "w", encoding="utf-8") as f:
                for word in self.words:
                    f.write(f"{word}\n")
            
            # Trigger engine reload by setting global config (unchanged)
            if self.dbus.iface:
                current_config = self.dbus.get_config()
                if current_config:
                    self.dbus.set_config(current_config.get("values", {}))

            self.initial_state = self._get_current_state()
        except Exception as e:
            QMessageBox.warning(self, _("Error"), _("Failed to save dictionary: {}").format(e))

    def upsert_row(self, word: str, sort: bool = True):
        if word in self.words:
            return
        self.words.append(word)
        if sort:
            self.words.sort()
        self.on_search_changed()
        self._on_item_changed()

    def _is_invalid_word(self, word: str) -> bool:
        """Checks if word contains spaces."""
        if not word:
            return False
        return " " in word

    def _apply_cell_highlight(self, item: QTableWidgetItem, word: str):
        """Applies red background and warning icon to items with invalid words."""
        is_invalid = self._is_invalid_word(word)
        bg_color = Qt.transparent
        tooltip = ""
        icon = QIcon()
        if is_invalid:
            bg_color = QColor(Qt.red)
            bg_color.setAlpha(60)
            icon = QIcon.fromTheme("dialog-warning")
            tooltip = _("Warning: Dictionary words should not contain spaces.")

        item.setBackground(bg_color)
        item.setToolTip(tooltip)
        item.setIcon(icon)

    def on_search_changed(self):
        """Filters the words and rebuilds the table."""
        search_text = self.search_input.text().lower().strip()
        if not search_text:
            self._rebuild_table()
            return

        filtered = [w for w in self.words if search_text in w.lower()]
        self._rebuild_table(filtered)

    def on_add(self):
        word = self.input_word.text().strip()
        if not word:
            return

        self.upsert_row(word)
        self.input_word.clear()
        self.input_word.setFocus()

    def _update_add_button_icon(self):
        """Handles validation and Add button state."""
        word = self.input_word.text().strip()
        is_invalid = self._is_invalid_word(word)
        
        if is_invalid:
            self.input_word.setStyleSheet("color: red;")
            self.input_word.setToolTip(_("Warning: Dictionary words should not contain spaces."))
        else:
            self.input_word.setStyleSheet("")
            self.input_word.setToolTip("")

        self.btn_add.setEnabled(not is_invalid and bool(word))

        if word in self.words:
            self.btn_add.setIcon(QIcon.fromTheme("document-save"))
            self.btn_add.setText(_("Exists"))
            self.btn_add.setEnabled(False)
        else:
            self.btn_add.setIcon(QIcon.fromTheme("list-add"))
            self.btn_add.setText(_("Add"))

    def on_cell_clicked(self, row, column):
        item = self.table.item(row, column)
        if item and item.text():
            self.input_word.setText(item.text())
        self.update_button_states()

    def on_remove(self):
        selected_items = self.table.selectedItems()
        if not selected_items:
            return

        for item in selected_items:
            word = item.text()
            if word in self.words:
                self.words.remove(word)

        self.on_search_changed()
        self.update_button_states()
        self._on_item_changed()
        self._update_add_button_icon()

    def do_import(self):
        path, _filter = QFileDialog.getOpenFileName(
            self,
            _("Import Custom Dictionary"),
            "",
            _("Dictionary files (*.tsv *.txt);;All files (*)"),
        )
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except (IOError, OSError, UnicodeDecodeError) as e:
            QMessageBox.warning(self, _("Error"), _("Cannot open file for reading: {}").format(e))
            return

        imported = 0
        confirmed = False
        for line in lines:
            word = line.strip()
            if not word or word.startswith("#"):
                continue

            if not confirmed and len(self.words) > 0:
                reply = QMessageBox.question(
                    self,
                    _("Confirm Import"),
                    _(
                        "The current dictionary is not empty. Imported entries will be merged. Continue?"
                    ),
                    QMessageBox.Yes | QMessageBox.No,
                )
                if reply == QMessageBox.No:
                    return
                confirmed = True
            else:
                confirmed = True

            if word not in self.words:
                self.words.append(word)
                imported += 1

        self.words.sort()
        self.on_search_changed()

        QMessageBox.information(
            self,
            _("Import Complete"),
            _("Imported {} words.").format(imported),
        )

    def do_export(self):
        if not self.words:
            QMessageBox.information(
                self, _("Export"), _("The custom dictionary is empty, nothing to export.")
            )
            return
        path, _filter = QFileDialog.getSaveFileName(
            self,
            _("Export Custom Dictionary"),
            "lotus-dict.tsv",
            _("Tab-separated (*.tsv);;Text files (*.txt);;All files (*)"),
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write("# Lotus Dictionary\n")
                for word in self.words:
                    f.write(f"{word}\n")
            QMessageBox.information(
                self,
                _("Export Complete"),
                _("Exported {} words to:\n{}").format(len(self.words), path),
            )
        except (IOError, OSError, UnicodeDecodeError) as e:
            QMessageBox.warning(self, _("Error"), _("Cannot open file for writing: {}").format(e))
