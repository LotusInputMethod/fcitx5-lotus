# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Backup and Restore page for Lotus settings.
Supports JSON-based backups and selective export/import.
"""

import os
import json
from datetime import datetime
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QFileDialog,
    QMessageBox,
    QFrame,
    QScrollArea,
    QCheckBox,
    QGroupBox,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
from i18n import _
from core.dbus_handler import LotusDBusHandler
from ui.pages.dynamic_settings import CardWidget


class BackupPage(QWidget):
    def __init__(self, dbus_handler: LotusDBusHandler, parent=None):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.restore_data = None  # Stores data from opened backup for selective restore
        self._setup_ui()

    def _setup_ui(self):
        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        content_widget = QWidget()
        layout = QVBoxLayout(content_widget)
        layout.setContentsMargins(30, 20, 30, 20)
        layout.setSpacing(20)

        title = QLabel(_("Backup & Restore"))
        title.setObjectName("CategoryTitle")
        layout.addWidget(title)

        # Single Card for Export / Import
        self.main_card = CardWidget(_("Export/Import Settings"))

        self.import_desc = QLabel(
            _(
                "Save or restore your configurations via JSON files. Select the components you wish to include:"
            )
        )
        self.import_desc.setWordWrap(True)
        self.import_desc.setStyleSheet("color: gray; font-size: 13px;")
        self.main_card.content_layout.addWidget(self.import_desc)

        # Shared Checkboxes
        self.checkboxes = {
            "config": QCheckBox(_("Main Settings")),
            "macros": QCheckBox(_("Macros")),
            "keymaps": QCheckBox(_("Custom Keymaps")),
            "rules": QCheckBox(_("Application Rules")),
            "dictionary": QCheckBox(_("Custom Dictionary")),
        }
        for cb in self.checkboxes.values():
            cb.setChecked(True)
            self.main_card.content_layout.addWidget(cb)

        # Buttons (Horizontal layout)
        btn_layout = QHBoxLayout()
        btn_layout.setSpacing(10)

        self.btn_export = QPushButton(
            QIcon.fromTheme("document-save-as"), _("Export Backup...")
        )
        self.btn_export.clicked.connect(self.do_export)
        self.btn_export.setMinimumHeight(40)

        self.btn_import = QPushButton(
            QIcon.fromTheme("document-open"), _("Import Backup...")
        )
        self.btn_import.clicked.connect(self.on_select_import_file)
        self.btn_import.setMinimumHeight(40)

        btn_layout.addWidget(self.btn_export)
        btn_layout.addWidget(self.btn_import)
        self.main_card.content_layout.addLayout(btn_layout)

        # Restore Confirmation (hidden initially)
        self.restore_group = QGroupBox(_("Items found in backup:"))
        self.restore_group_layout = QVBoxLayout(self.restore_group)
        self.restore_group.setVisible(False)
        self.main_card.content_layout.addWidget(self.restore_group)

        self.btn_restore = QPushButton(
            QIcon.fromTheme("system-reboot"), _("Restore Selected Now")
        )
        self.btn_restore.clicked.connect(self.on_restore_selected)
        self.btn_restore.setMinimumHeight(40)
        self.btn_restore.setVisible(False)
        self.btn_restore.setStyleSheet("font-weight: bold;")
        self.main_card.content_layout.addWidget(self.btn_restore)

        layout.addWidget(self.main_card)

        layout.addStretch()
        scroll.setWidget(content_widget)
        root_layout.addWidget(scroll)

    def _get_local_dict_path(self) -> str:
        xdg_data_home = os.environ.get(
            "XDG_DATA_HOME", os.path.expanduser("~/.local/share")
        )
        return os.path.join(xdg_data_home, "fcitx5/lotus/vietnamese.cm.dict")

    def do_export(self):
        """Creates a JSON backup of selected components."""
        selected = {k: cb.isChecked() for k, cb in self.checkboxes.items()}
        if not any(selected.values()):
            QMessageBox.warning(
                self, _("Warning"), _("Please select at least one item to export.")
            )
            return

        default_filename = (
            f"lotus-backup-{datetime.now().strftime('%Y%m%d-%H%M%S')}.json"
        )
        path, _filter = QFileDialog.getSaveFileName(
            self,
            _("Export Backup"),
            os.path.join(os.path.expanduser("~"), default_filename),
            _("JSON Backup (*.json);;All Files (*)"),
        )
        if not path:
            return

        try:
            backup = {
                "meta": {
                    "version": 1,
                    "timestamp": datetime.now().isoformat(),
                    "components": [k for k, v in selected.items() if v],
                }
            }

            if selected["config"]:
                config_data = self.dbus.get_config().get("values", {})
                backup["config"] = config_data

            if selected["macros"]:
                macros = self.dbus.get_sub_config_list("lotus-macro", "Macro")
                backup["macros"] = macros

            if selected["keymaps"]:
                keymaps = self.dbus.get_sub_config_list("custom_keymap", "CustomKeymap")
                backup["keymaps"] = keymaps

            if selected["rules"]:
                rules = self.dbus.get_sub_config_list("app_rules", "Rules")
                backup["rules"] = rules

            if selected["dictionary"]:
                dict_path = self._get_local_dict_path()
                if os.path.exists(dict_path):
                    with open(dict_path, "r", encoding="utf-8") as f:
                        backup["dictionary"] = f.read()

            with open(path, "w", encoding="utf-8") as f:
                json.dump(backup, f, indent=2, ensure_ascii=False)

            QMessageBox.information(
                self, _("Success"), _("Backup exported successfully to:\n") + path
            )

        except Exception as e:
            QMessageBox.critical(
                self, _("Error"), _("Failed to export backup:\n") + str(e)
            )

    def on_select_import_file(self):
        """Opens a JSON backup and shows available components for restore."""
        path, _filter = QFileDialog.getOpenFileName(
            self,
            _("Select Backup File"),
            os.path.expanduser("~"),
            _("JSON Backup (*.json);;All Files (*)"),
        )
        if not path:
            return

        try:
            # Re-init UI for restore
            for i in reversed(range(self.restore_group_layout.count())):
                widget = self.restore_group_layout.itemAt(i).widget()
                if widget:
                    widget.deleteLater()

            self.restore_checkboxes = {}
            self.restore_data = {"json_path": path}

            with open(path, "r", encoding="utf-8") as f:
                backup = json.load(f)

            options = [
                ("config", _("Main Settings")),
                ("macros", _("Macros")),
                ("keymaps", _("Custom Keymaps")),
                ("rules", _("Application Rules")),
                ("dictionary", _("Custom Dictionary")),
            ]

            found_any = False
            for key, label in options:
                if key in backup:
                    cb = QCheckBox(label)
                    cb.setChecked(True)
                    self.restore_checkboxes[key] = cb
                    self.restore_group_layout.addWidget(cb)
                    found_any = True

            if not found_any:
                raise ValueError(
                    _("Invalid backup file: No recognizable components found.")
                )

            self.restore_group.setVisible(True)
            self.btn_restore.setVisible(True)

        except Exception as e:
            QMessageBox.critical(
                self, _("Error"), _("Failed to open backup file:\n") + str(e)
            )

    def on_restore_selected(self):
        """Applies selected components from the JSON backup."""
        if not self.restore_data or "json_path" not in self.restore_data:
            return

        selected_keys = [
            k for k, cb in self.restore_checkboxes.items() if cb.isChecked()
        ]
        if not selected_keys:
            QMessageBox.warning(
                self, _("Warning"), _("Please select at least one item to restore.")
            )
            return

        reply = QMessageBox.warning(
            self,
            _("Confirm Restore"),
            _(
                "Are you sure you want to restore the selected components? This will overwrite your current configuration."
            ),
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply != QMessageBox.Yes:
            return

        try:
            with open(self.restore_data["json_path"], "r", encoding="utf-8") as f:
                backup = json.load(f)

            if "config" in selected_keys and "config" in backup:
                self.dbus.set_config(backup["config"])

            if "macros" in selected_keys and "macros" in backup:
                self.dbus.set_sub_config_list("lotus-macro", "Macro", backup["macros"])

            if "keymaps" in selected_keys and "keymaps" in backup:
                self.dbus.set_sub_config_list(
                    "custom_keymap", "CustomKeymap", backup["keymaps"]
                )

            if "rules" in selected_keys and "rules" in backup:
                self.dbus.set_sub_config_list("app_rules", "Rules", backup["rules"])

            if "dictionary" in selected_keys and "dictionary" in backup:
                dict_path = self._get_local_dict_path()
                os.makedirs(os.path.dirname(dict_path), exist_ok=True)
                with open(dict_path, "w", encoding="utf-8") as f:
                    f.write(backup["dictionary"])

            QMessageBox.information(
                self,
                _("Success"),
                _(
                    "Selected components restored successfully. Some changes may require restarting Fcitx5."
                ),
            )

            # Trigger UI reload
            main_win = self.window()
            if hasattr(main_win, "on_cancel"):
                main_win.on_cancel()

            # Reset Restore UI
            self.restore_group.setVisible(False)
            self.btn_restore.setVisible(False)
            self.restore_data = None

        except Exception as e:
            QMessageBox.critical(
                self, _("Error"), _("Failed to restore backup:\n") + str(e)
            )
