# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Main window assembling all configuration tabs with a modern layout.
"""

from PyQt6.QtWidgets import (
    QMainWindow,
    QWidget,
    QHBoxLayout,
    QVBoxLayout,
    QListWidget,
    QStackedWidget,
    QListWidgetItem,
    QApplication,
    QFrame,
    QPushButton,
)
from PyQt6.QtGui import QIcon
from PyQt6.QtCore import Qt, QSize
from i18n import _
from core.dbus_handler import LotusDBusHandler


class LotusSettingsWindow(QMainWindow):
    """Main entry window for Lotus Configuration GUI."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle(_("Lotus Settings"))

        self.dbus_handler = LotusDBusHandler()

        self._setup_ui()
        self._setup_window_size()
        # self._apply_global_styles()
        self.update_reset_button_state()

    def update_reset_button_state(self):
        any_modified_from_default = any(
            (
                hasattr(self.content_stack.widget(i), "is_modified_from_default")
                and self.content_stack.widget(i).is_modified_from_default()
            )
            or (
                hasattr(self.content_stack.widget(i), "is_modified")
                and self.content_stack.widget(i).is_modified()
            )
            for i in range(self.content_stack.count())
        )
        self.btn_reset.setEnabled(any_modified_from_default)

    def _apply_global_styles(self):
        self.setStyleSheet("""
            QLabel#CategoryTitle {
                font-size: 22px;
            }
            QLabel#AboutTitle {
                font-size: 26px;
            }
            QLabel#KeyCap {
                background-color: palette(button);
                color: palette(button-text);
                border: 1px solid palette(mid);
                border-bottom: 2px solid palette(dark);
                border-radius: 4px;
                padding: 2px 6px;
                font-weight: bold;
                font-family: monospace;
            }
            QLabel#ShortcutWarning {
                color: palette(link-visited);
                font-size: 12px;
            }
        """)

    def _setup_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        main_v_layout = QVBoxLayout(central_widget)
        main_v_layout.setContentsMargins(0, 0, 0, 0)
        main_v_layout.setSpacing(0)

        main_h_layout = QHBoxLayout()
        main_h_layout.setContentsMargins(0, 0, 0, 0)
        main_h_layout.setSpacing(0)

        self.sidebar = QListWidget()
        self.sidebar.setFixedWidth(200)
        self.sidebar.setStyleSheet("""
            QListWidget {
                border: none;
                background: transparent;
                outline: none;
                padding-top: 15px;
            }
            QListWidget::item {
                padding: 10px 15px;
                border-radius: 8px;
                margin: 2px 10px;
            }
            QListWidget::item:selected {
                background: palette(highlight);
                color: palette(highlighted-text);
            }
            QListWidget::item:hover:!selected {
                background: palette(alternate-base);
            }
        """)
        self.sidebar.setObjectName("Sidebar")
        self.sidebar.setFrameShape(QFrame.Shape.NoFrame)

        self.content_stack = QStackedWidget()

        main_h_layout.addWidget(self.sidebar)
        main_h_layout.addWidget(self.content_stack, 1)

        main_v_layout.addLayout(main_h_layout, 1)

        # Bottom Bar
        self._setup_bottom_bar(main_v_layout)

        # Pages Mapping
        self._setup_pages()

        self.sidebar.currentRowChanged.connect(self._on_sidebar_changed)
        self.sidebar.setCurrentRow(0)

    def _setup_bottom_bar(self, layout):
        container = QFrame()
        container.setObjectName("BottomBar")
        bar_layout = QHBoxLayout(container)
        bar_layout.setContentsMargins(20, 12, 20, 12)
        bar_layout.setSpacing(10)

        bar_layout.addSpacing(180)

        self.btn_reset = QPushButton(QIcon.fromTheme("edit-undo"), _("&Reset"))
        self.btn_reset.clicked.connect(self.on_restore_defaults)
        bar_layout.addWidget(self.btn_reset)

        bar_layout.addStretch()

        self.btn_cancel = QPushButton(QIcon.fromTheme("dialog-cancel"), _("&Cancel"))
        self.btn_cancel.setEnabled(False)
        self.btn_cancel.clicked.connect(self.on_cancel)
        bar_layout.addWidget(self.btn_cancel)

        self.btn_apply = QPushButton(QIcon.fromTheme("document-save"), _("&Apply"))
        self.btn_apply.setEnabled(False)
        self.btn_apply.clicked.connect(lambda: self.on_save_all(quiet=True))
        bar_layout.addWidget(self.btn_apply)

        self.btn_ok = QPushButton(QIcon.fromTheme("dialog-ok"), _("&OK"))
        self.btn_ok.setObjectName("Primary")
        self.btn_ok.clicked.connect(self.on_ok)
        bar_layout.addWidget(self.btn_ok)

        layout.addWidget(container)

    def _setup_pages(self):
        from ui.pages.dynamic_settings import SettingsCategory

        self._add_page(_("General"), "preferences-system", "general",
                       lambda: __import__("ui.pages.dynamic_settings", fromlist=["DynamicSettingsPage"]).DynamicSettingsPage(self.dbus_handler, category=SettingsCategory.GENERAL))
        self._add_page(_("Keymap"), "input-keyboard", "keymap",
                       lambda: __import__("ui.pages.keymap_editor", fromlist=["KeymapEditorPage"]).KeymapEditorPage(self.dbus_handler))
        self._add_page(_("Dictionary"), "accessories-dictionary", "dict",
                       lambda: __import__("ui.pages.dict_editor", fromlist=["DictEditorPage"]).DictEditorPage(self.dbus_handler))
        self._add_page(_("Macro"), "text-x-generic", "macro",
                       lambda: __import__("ui.pages.macro_editor", fromlist=["MacroEditorPage"]).MacroEditorPage(self.dbus_handler))
        self._add_page(_("Mode Manager"), "preferences-desktop", "mode",
                       lambda: __import__("ui.pages.mode_manager", fromlist=["ModeManagerPage"]).ModeManagerPage(self.dbus_handler))
        self._add_page(_("Backup & Restore"), "drive-harddisk", "backup",
                       lambda: __import__("ui.pages.backup", fromlist=["BackupPage"]).BackupPage(self.dbus_handler))
        self._add_page(_("About"), "help-about", "about",
                       lambda: __import__("ui.pages.about", fromlist=["AboutPage"]).AboutPage(self.dbus_handler))

        if self.sidebar.count() > 0:
            self.sidebar.setCurrentRow(0)
            first_item = self.sidebar.item(0)
            if first_item:
                self._on_sidebar_changed(first_item)


    def on_restore_defaults(self):
        """Resets all settings to their default values."""
        from PyQt6.QtWidgets import QMessageBox

        reply = QMessageBox.question(
            self,
            _("Confirm Reset"),
            _("Restore all settings to defaults?"),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if reply == QMessageBox.StandardButton.Yes:
            for i in range(self.content_stack.count()):
                page = self.content_stack.widget(i)
                if hasattr(page, "restore_defaults"):
                    page.restore_defaults()
            self.on_changed()

    def on_changed(self):
        """Enables/disables the apply and cancel buttons based on pending changes."""
        any_modified = any(
            hasattr(self.content_stack.widget(i), "is_modified")
            and self.content_stack.widget(i).is_modified()
            for i in range(self.content_stack.count())
        )
        has_errors = self.has_validation_errors()
        self.btn_apply.setEnabled(any_modified and not has_errors)
        self.btn_cancel.setEnabled(any_modified)
        self.btn_ok.setEnabled(not has_errors)
        self.update_reset_button_state()

    def has_validation_errors(self):
        return any(
            hasattr(self.content_stack.widget(i), "has_validation_errors")
            and self.content_stack.widget(i).has_validation_errors()
            for i in range(self.content_stack.count())
        )

    def validation_message(self):
        messages = []
        for i in range(self.content_stack.count()):
            page = self.content_stack.widget(i)
            if hasattr(page, "validation_message"):
                message = page.validation_message()
                if message:
                    messages.append(message)
        return "\n".join(messages)

    def on_save_all(self, quiet=False):
        """Triggers save on all pages that support it."""
        if self.has_validation_errors():
            from PyQt6.QtWidgets import QMessageBox

            QMessageBox.warning(
                self,
                _("Cannot Save"),
                self.validation_message(),
            )
            return False

        for i in range(self.content_stack.count()):
            page = self.content_stack.widget(i)
            if hasattr(page, "save_data"):
                if page.save_data() is False:
                    return False

        self.btn_apply.setEnabled(False)
        self.btn_cancel.setEnabled(False)
        self.update_reset_button_state()
        if not quiet:
            from PyQt6.QtWidgets import QMessageBox

            QMessageBox.information(self, _("Success"), _("Settings saved."))
        return True

    def on_ok(self):
        if self.on_save_all(quiet=True):
            self.close()

    def on_cancel(self):
        """Discards all unsaved changes by reloading data on all pages."""
        for i in range(self.content_stack.count()):
            page = self.content_stack.widget(i)
            if hasattr(page, "load_data"):
                page.load_data()
            elif hasattr(page, "load_config"):
                page.load_config()

        self.btn_apply.setEnabled(False)
        self.btn_cancel.setEnabled(False)
        self.btn_ok.setEnabled(not self.has_validation_errors())
        self.update_reset_button_state()

    def _on_sidebar_changed(self, current, previous=None):
        if isinstance(current, int):
            current_item = self.sidebar.item(current)
        else:
            current_item = current

        if not current_item:
            return

        page_id = current_item.data(Qt.ItemDataRole.UserRole)
        if not page_id or not hasattr(self, "_pages") or page_id not in self._pages:
            return

        page_info = self._pages[page_id]
        if page_info.get("instance") is None:
            page_info["instance"] = page_info["factory"]()
            self.content_stack.addWidget(page_info["instance"])

        self.content_stack.setCurrentWidget(page_info["instance"])


    def _setup_window_size(self):
        screen = QApplication.primaryScreen().availableGeometry()
        w = int(screen.width() * 0.45)
        h = int(screen.height() * 0.60)
        self.setMinimumSize(750, 500)
        self.resize(w, h)
        self.move((screen.width() - w) // 2, (screen.height() - h) // 2)

    def _add_page(self, title, icon_name, page_id, factory):
        if not hasattr(self, "_pages"):
            self._pages = {}
        item = QListWidgetItem(QIcon.fromTheme(icon_name), title)
        item.setData(Qt.ItemDataRole.UserRole, page_id)
        self.sidebar.addItem(item)
        self._pages[page_id] = {"factory": factory, "instance": None}

