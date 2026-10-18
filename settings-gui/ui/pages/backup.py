# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Backup and Restore page for Lotus settings.
Supports JSON-based backups and selective export/import.
"""

import json
import os
from datetime import datetime

from core.backup_logic import (
    COMPONENT_LABELS,
    backup_file_name,
    build_backup_meta,
    collect_backup,
    present_component_keys,
    restore_backup,
)
from core.dbus_handler import LotusDBusHandler
from i18n import _
from qtpy.QtGui import QIcon
from qtpy.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QFrame,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from ui.components import ScrollContent
from ui.helpers import local_dict_path
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

        content_widget = ScrollContent()
        layout = QVBoxLayout(content_widget)
        layout.setContentsMargins(30, 20, 30, 20)
        layout.setSpacing(20)

        title = QLabel(_("Backup & Restore"))
        title.setObjectName("CategoryTitle")
        layout.addWidget(title)

        # Single Card for Export / Import
        self.main_card = CardWidget(_("Export/Import Settings"))

        self.import_desc = QLabel(
            _("Save or restore configuration via JSON files. Select components to include:")
        )
        self.import_desc.setWordWrap(True)
        self.import_desc.setStyleSheet("color: gray; font-size: 13px;")
        self.main_card.content_layout.addWidget(self.import_desc)

        # Shared Checkboxes
        self.checkboxes = {key: QCheckBox(_(label)) for key, label in COMPONENT_LABELS.items()}
        for cb in self.checkboxes.values():
            cb.setChecked(True)
            self.main_card.content_layout.addWidget(cb)

        # Buttons (Horizontal layout)
        btn_layout = QHBoxLayout()
        btn_layout.setSpacing(10)

        self.btn_export = QPushButton(QIcon.fromTheme("document-save-as"), _("Export Backup..."))
        self.btn_export.clicked.connect(self.do_export)
        self.btn_export.setMinimumHeight(40)

        self.btn_import = QPushButton(QIcon.fromTheme("document-open"), _("Import Backup..."))
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

        self.btn_restore = QPushButton(QIcon.fromTheme("system-reboot"), _("Restore Selected Now"))
        self.btn_restore.clicked.connect(self.on_restore_selected)
        self.btn_restore.setMinimumHeight(40)
        self.btn_restore.setVisible(False)
        self.btn_restore.setStyleSheet("font-weight: bold;")
        self.main_card.content_layout.addWidget(self.btn_restore)

        layout.addWidget(self.main_card)

        layout.addStretch()
        scroll.setWidget(content_widget)
        root_layout.addWidget(scroll)

    def do_export(self):
        """Creates a JSON backup of selected components."""
        selected_keys = [k for k, cb in self.checkboxes.items() if cb.isChecked()]
        if not selected_keys:
            QMessageBox.warning(self, _("Warning"), _("Select at least one item to export."))
            return

        path, _filter = QFileDialog.getSaveFileName(
            self,
            _("Export Backup"),
            os.path.join(os.path.expanduser("~"), backup_file_name(datetime.now())),
            _("JSON Backup (*.json);;All Files (*)"),
        )
        if not path:
            return

        try:
            backup = {"meta": build_backup_meta(selected_keys, datetime.now())}
            backup.update(collect_backup(self.dbus, selected_keys, local_dict_path()))

            with open(path, "w", encoding="utf-8") as f:
                json.dump(backup, f, indent=2, ensure_ascii=False)

            QMessageBox.information(self, _("Success"), _("Backup exported to:\n") + path)

        except Exception as e:
            QMessageBox.critical(self, _("Error"), _("Failed to export backup:\n") + str(e))

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

            found_any = False
            for key in present_component_keys(backup):
                cb = QCheckBox(_(COMPONENT_LABELS[key]))
                cb.setChecked(True)
                self.restore_checkboxes[key] = cb
                self.restore_group_layout.addWidget(cb)
                found_any = True

            if not found_any:
                raise ValueError(_("Invalid backup file: No recognizable components found."))

            self.restore_group.setVisible(True)
            self.btn_restore.setVisible(True)

        except Exception as e:
            QMessageBox.critical(self, _("Error"), _("Failed to open backup file:\n") + str(e))

    def on_restore_selected(self):
        """Applies selected components from the JSON backup."""
        if not self.restore_data or "json_path" not in self.restore_data:
            return

        selected_keys = [k for k, cb in self.restore_checkboxes.items() if cb.isChecked()]
        if not selected_keys:
            QMessageBox.warning(self, _("Warning"), _("Select at least one item to restore."))
            return

        reply = QMessageBox.warning(
            self,
            _("Confirm Restore"),
            _(
                "Are you sure you want to restore selected components? This will overwrite current configuration."
            ),
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply != QMessageBox.Yes:
            return

        try:
            with open(self.restore_data["json_path"], "r", encoding="utf-8") as f:
                backup = json.load(f)

            failed_components = restore_backup(self.dbus, backup, selected_keys, local_dict_path())

            if failed_components:
                QMessageBox.critical(
                    self,
                    _("Error"),
                    _("Failed to restore the following components:\n- ")
                    + "\n- ".join(_(COMPONENT_LABELS[key]) for key in failed_components),
                )
                return

            QMessageBox.information(
                self,
                _("Success"),
                _("Selected components restored. Fcitx5 restart may be required for some changes."),
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
            QMessageBox.critical(self, _("Error"), _("Failed to restore backup:\n") + str(e))
