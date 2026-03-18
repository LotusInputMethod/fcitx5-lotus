# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Main window assembling all configuration tabs with a modern layout.
"""

from PySide6.QtWidgets import (
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
    QSizePolicy,
)
from PySide6.QtGui import QIcon
from PySide6.QtCore import Qt, QSize, QFile
from i18n import _
from core.dbus_handler import LotusDBusHandler
from core.file_handler import Fcitx5ConfigHandler

from ui.pages.dynamic_settings import DynamicSettingsPage, SettingsCategory
from ui.pages.macro_editor import MacroEditorPage
from ui.pages.keymap_editor import KeymapEditorPage
from ui.pages.about import AboutPage
import os


class LotusSettingsWindow(QMainWindow):
    """Main entry window for Lotus Configuration GUI."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle(_("Lotus Settings"))

        self.dbus_handler = LotusDBusHandler()
        self.file_handler = Fcitx5ConfigHandler()

        self._setup_ui()
        self._setup_window_size()
        self._apply_global_styles()
        self.update_reset_button_state()

    def update_reset_button_state(self):
        any_modified = any(
            hasattr(self.content_stack.widget(i), "is_modified_from_default")
            and self.content_stack.widget(i).is_modified_from_default()
            for i in range(self.content_stack.count())
        )
        self.btn_reset.setEnabled(any_modified)

    def _apply_global_styles(self):
        self.setStyleSheet("""
            QLabel#CategoryTitle {
                font-size: 22px;
            }
            QLabel#AboutTitle {
                font-size: 26px;
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
        self.sidebar.setStyleSheet(
            """
            QListWidget {
                border: none;
                background: transparent;
                outline: none;
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
        """
        )
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
        self.btn_apply.clicked.connect(lambda: self.on_save_all(quiet=True))
        bar_layout.addWidget(self.btn_apply)

        self.btn_ok = QPushButton(QIcon.fromTheme("dialog-ok"), _("&OK"))
        self.btn_ok.setObjectName("Primary")
        self.btn_ok.clicked.connect(self.on_ok)
        bar_layout.addWidget(self.btn_ok)

        layout.addWidget(container)

    def _setup_pages(self):
        # Top-level Settings Pages
        self._add_page(
            _("General"),
            "preferences-system",
            DynamicSettingsPage(self.dbus_handler, category=SettingsCategory.GENERAL),
        )
        self._add_page(
            _("Typing"),
            "input-keyboard",
            DynamicSettingsPage(self.dbus_handler, category=SettingsCategory.TYPING),
        )
        self._add_page(
            _("Macros"),
            "accessories-text-editor",
            MacroEditorPage(self.file_handler, self.dbus_handler),
        )
        self._add_page(
            _("Keymap"),
            "preferences-desktop-keyboard",
            KeymapEditorPage(self.file_handler),
        )
        self._add_page(
            _("Appearance"),
            "preferences-desktop-theme",
            DynamicSettingsPage(
                self.dbus_handler, category=SettingsCategory.APPEARANCE
            ),
        )

        # Bottom section
        spacer = QListWidgetItem()
        spacer.setFlags(Qt.NoItemFlags)
        spacer.setSizeHint(QSize(0, 20))
        self.sidebar.addItem(spacer)
        self._add_page(_("About"), "help-about", AboutPage())

    def on_restore_defaults(self):
        """Resets all settings to their default values."""
        from PySide6.QtWidgets import QMessageBox

        reply = QMessageBox.question(
            self,
            _("Confirm Reset"),
            _("Are you sure you want to restore all settings to their default values?"),
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
        """Enables the apply and cancel buttons when changes are detected."""
        self.btn_apply.setEnabled(True)
        self.btn_cancel.setEnabled(True)
        self.update_reset_button_state()

    def on_save_all(self, quiet=False):
        """Triggers save on all pages that support it."""
        for i in range(self.content_stack.count()):
            page = self.content_stack.widget(i)
            if hasattr(page, "save_data"):
                page.save_data(quiet=True)

        self.btn_apply.setEnabled(False)
        self.btn_cancel.setEnabled(False)
        self.update_reset_button_state()
        if not quiet:
            from PySide6.QtWidgets import QMessageBox

            QMessageBox.information(
                self, _("Success"), _("All settings applied successfully.")
            )

    def on_ok(self):
        self.on_save_all(quiet=True)
        self.close()

    def on_cancel(self):
        """Discards all unsaved changes by reloading data on all pages."""
        for i in range(self.content_stack.count()):
            page = self.content_stack.widget(i)
            if hasattr(page, "restore_defaults"):
                page.restore_defaults()

        self.btn_apply.setEnabled(False)
        self.btn_cancel.setEnabled(False)
        self.update_reset_button_state()

    def _on_sidebar_changed(self, index):
        item = self.sidebar.item(index)
        if not item:
            return

        role = item.data(Qt.UserRole)
        if role == "page":
            widget = item.data(Qt.UserRole + 1)
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
        h = int(screen.height() * 0.55)
        self.setMinimumSize(750, 500)
        self.resize(w, h)
        self.move((screen.width() - w) // 2, (screen.height() - h) // 2)

    def _add_header(self, title: str):
        item = QListWidgetItem(title)
        item.setData(Qt.UserRole, "header")
        item.setFlags(item.flags() & ~Qt.ItemIsSelectable)

        # Set styling directly since QListWidgetItem is not a QObject
        font = item.font()
        font.setBold(True)
        font.setPointSize(10)
        item.setFont(font)
        item.setForeground(Qt.gray)

        self.sidebar.addItem(item)

    def _add_page(self, title: str, icon_name: str, widget: QWidget):
        item = QListWidgetItem(QIcon.fromTheme(icon_name), title)
        item.setData(Qt.UserRole, "page")

        self.content_stack.addWidget(widget)
        item.setData(Qt.UserRole + 1, widget)

        self.sidebar.addItem(item)
