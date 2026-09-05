# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Main window assembling all configuration tabs with a modern layout.
"""

from qtpy.QtWidgets import (
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
    QSpacerItem,
)
from qtpy.QtGui import QIcon, QPalette
from qtpy.QtCore import Qt, QSize
from i18n import _
from core.dbus_handler import LotusDBusHandler

# Lazy loading pages on demand


class LotusSettingsWindow(QMainWindow):
    """Main entry window for Lotus Configuration GUI."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle(_("Lotus Settings"))

        self.dbus_handler = LotusDBusHandler()

        self._setup_ui()
        self._setup_window_size()
        self._apply_global_styles()
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
        self.sidebar.setFrameShape(QFrame.NoFrame)

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
        self.btn_apply.clicked.connect(lambda: self.on_save_all(quiet=False))
        bar_layout.addWidget(self.btn_apply)

        self.btn_ok = QPushButton(QIcon.fromTheme("dialog-ok"), _("&OK"))
        self.btn_ok.setObjectName("Primary")
        self.btn_ok.clicked.connect(self.on_ok)
        bar_layout.addWidget(self.btn_ok)

        layout.addWidget(container)

    def _setup_pages(self):
        def create_general():
            from ui.pages.dynamic_settings import DynamicSettingsPage, SettingsCategory

            return DynamicSettingsPage(
                self.dbus_handler, category=SettingsCategory.GENERAL
            )

        def create_typing():
            from ui.pages.dynamic_settings import DynamicSettingsPage, SettingsCategory

            return DynamicSettingsPage(
                self.dbus_handler, category=SettingsCategory.TYPING
            )

        def create_applications():
            from ui.pages.mode_manager import ModeManagerPage

            return ModeManagerPage(self.dbus_handler)

        def create_macros():
            from ui.pages.macro_editor import MacroEditorPage

            return MacroEditorPage(self.dbus_handler)

        def create_dict():
            from ui.pages.dict_editor import DictEditorPage

            return DictEditorPage(self.dbus_handler)

        def create_keymap():
            from ui.pages.keymap_editor import KeymapEditorPage

            return KeymapEditorPage(self.dbus_handler)

        def create_shortcuts():
            from ui.pages.dynamic_settings import DynamicSettingsPage, SettingsCategory

            return DynamicSettingsPage(
                self.dbus_handler, category=SettingsCategory.SHORTCUTS
            )

        def create_appearance():
            from ui.pages.dynamic_settings import DynamicSettingsPage, SettingsCategory

            return DynamicSettingsPage(
                self.dbus_handler, category=SettingsCategory.APPEARANCE
            )

        def create_backup():
            from ui.pages.backup import BackupPage

            return BackupPage(self.dbus_handler)

        def create_about():
            from ui.pages.about import AboutPage

            return AboutPage()

        self._add_page(_("General"), "preferences-system", create_general)
        self._add_page(_("Typing"), "input-keyboard", create_typing)
        self._add_page(_("Applications"), "applications-other", create_applications)
        self._add_page(_("Macros"), "accessories-text-editor", create_macros)
        self._add_page(_("Dictionary"), "edit-copy", create_dict)
        self._add_page(_("Keymap"), "preferences-desktop-keyboard", create_keymap)
        self._add_page(
            _("Shortcuts"), "preferences-desktop-keyboard-shortcuts", create_shortcuts
        )
        self._add_page(_("Appearance"), "preferences-desktop-theme", create_appearance)
        self._add_page(_("Backup"), "document-save-as", create_backup)

        spacer = QListWidgetItem()
        spacer.setFlags(Qt.NoItemFlags)
        spacer.setSizeHint(QSize(0, 20))
        self.sidebar.addItem(spacer)
        self._add_page(_("About"), "help-about", create_about)

    def on_restore_defaults(self):
        """Resets all settings to their default values."""
        from qtpy.QtWidgets import QMessageBox

        reply = QMessageBox.question(
            self,
            _("Confirm Reset"),
            _("Restore all settings to defaults?"),
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply == QMessageBox.Yes:
            for i in range(self.content_stack.count()):
                page = self.content_stack.widget(i)
                if hasattr(page, "restore_defaults"):
                    page.restore_defaults()
            # After reset, we definitely have "unsaved changes" relative to previous
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
        from qtpy.QtWidgets import QMessageBox

        if self.has_validation_errors():
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
                    QMessageBox.critical(
                        self,
                        _("Error"),
                        _("Failed to save settings. Please check if Fcitx5 is running."),
                    )
                    return False

        self.btn_apply.setEnabled(False)
        self.btn_cancel.setEnabled(False)
        self.update_reset_button_state()
        if not quiet:
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

    def _on_sidebar_changed(self, index):
        item = self.sidebar.item(index)
        if not item:
            return

        role = item.data(Qt.UserRole)
        if role == "page":
            widget = item.data(Qt.UserRole + 2)
            if widget is None:
                factory = item.data(Qt.UserRole + 1)
                if factory:
                    widget = factory()
                    self.content_stack.addWidget(widget)
                    item.setData(Qt.UserRole + 2, widget)
            if widget:
                self.content_stack.setCurrentWidget(widget)
            self.update_reset_button_state()
        elif role == "header":
            # Don't allow selecting headers, move to next item
            if index + 1 < self.sidebar.count():
                self.sidebar.setCurrentRow(index + 1)

    def _setup_window_size(self):
        screen = QApplication.primaryScreen().availableGeometry()
        w = int(screen.width() * 0.45)
        h = int(screen.height() * 0.60)
        self.setMinimumSize(750, 500)
        self.resize(w, h)
        self.move((screen.width() - w) // 2, (screen.height() - h) // 2)

    def _add_page(self, title: str, icon_name: str, page_factory):
        item = QListWidgetItem(QIcon.fromTheme(icon_name), title)
        item.setData(Qt.UserRole, "page")
        item.setData(Qt.UserRole + 1, page_factory)
        item.setData(Qt.UserRole + 2, None)

        self.sidebar.addItem(item)
