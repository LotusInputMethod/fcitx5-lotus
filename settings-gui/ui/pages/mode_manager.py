# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Mode Manager Page for per-application input mode configuration.
"""

import copy
import os

from core.app_catalog import (
    desktop_search_paths,
    resolve_icon_name,
    scan_desktop_icons,
)
from core.app_rules import (
    app_matches_filter,
    apply_app_mode,
    parse_app_rules,
    rule_delay,
    serialize_app_rules,
)
from core.config_values import coerce_int
from core.dbus_handler import LotusDBusHandler
from core.mode_types import (
    DEFAULT_DELAY_KEYS,
    MODE_DEFAULT,
    MODE_EMOJI,
    MODE_INFO,
    MODE_MINECRAFT,
    MODE_OFF,
    MODE_PREEDIT,
    MODE_SELECT,
    MODE_SMOOTH,
    MODE_SUPER_SMOOTH,
    MODE_SURROUNDING,
    SELECTABLE_MODES,
)
from i18n import _
from qtpy.QtCore import QSize, Qt, Signal
from qtpy.QtGui import QIcon
from qtpy.QtWidgets import (
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ui.components import ScrollContent
from ui.helpers import notify_parent_changed
from ui.pages.dynamic_settings import CardWidget

# Re-exported mode definitions keep the page-level API stable.
__all__ = [
    "DEFAULT_DELAY_KEYS",
    "MODE_DEFAULT",
    "MODE_EMOJI",
    "MODE_INFO",
    "MODE_MINECRAFT",
    "MODE_OFF",
    "MODE_PREEDIT",
    "MODE_SELECT",
    "MODE_SMOOTH",
    "MODE_SUPER_SMOOTH",
    "MODE_SURROUNDING",
    "ModeCard",
    "ModeManagerPage",
    "SELECTABLE_MODES",
]


class ModeCard(QFrame):
    """A card for selecting an input mode."""

    clicked = Signal(int)

    def __init__(self, mode: int, selected: bool = False, parent=None):
        super().__init__(parent)
        self.mode = mode
        self.selected = selected
        self.setObjectName("ModeCard")
        self.setCursor(Qt.PointingHandCursor)
        self._setup_ui()
        self.update_style()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 15, 10, 15)
        layout.setSpacing(5)

        info = MODE_INFO[self.mode]
        title_label = QLabel(_(info["title"]))
        title_label.setObjectName("ModeCardTitle")
        title_label.setAlignment(Qt.AlignCenter)
        title_label.setWordWrap(True)
        title_label.setStyleSheet("font-weight: bold; font-size: 13px;")

        layout.addWidget(title_label)

    def update_style(self):
        if self.selected:
            self.setStyleSheet(
                """
                QFrame#ModeCard {
                    border: 1.5px solid palette(highlight);
                    background: palette(highlight);
                    border-radius: 8px;
                }
                QLabel { color: palette(highlighted-text); }
            """
            )
        else:
            self.setStyleSheet(
                """
                QFrame#ModeCard {
                    border: 1.5px solid palette(mid);
                    background: palette(alternate-base);
                    border-radius: 8px;
                }
            """
            )

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit(self.mode)


class ModeManagerPage(QWidget):
    """Main Mode Manager page."""

    def __init__(self, dbus_handler: LotusDBusHandler, parent=None):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.app_rules = {}
        self.original_app_rules = {}
        self.original_global_mode = ""
        self.default_delay_spins = {}
        self.original_default_delays = {}
        self.modified_default_delays = {}
        self.selected_app = None
        self.current_app_mode = MODE_DEFAULT
        self._icon_cache = {}
        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        self.layout = QHBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)

        # Left Sidebar (Expanded)
        self.sidebar_widget = QWidget()
        self.sidebar_widget.setFixedWidth(240)
        self.sidebar_layout = QVBoxLayout(self.sidebar_widget)
        self.sidebar_layout.setContentsMargins(15, 20, 15, 20)
        self.app_search = QLineEdit()
        self.app_search.setPlaceholderText(_("Search applications..."))
        self.app_search.textChanged.connect(self._filter_apps)
        self.sidebar_layout.addWidget(self.app_search)

        self.sidebar_layout.setSpacing(10)

        self.app_list = QListWidget()
        self.app_list.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.app_list.setTextElideMode(Qt.ElideRight)
        self.app_list.setIconSize(QSize(24, 24))
        self.app_list.itemClicked.connect(self._on_app_selected)
        self.sidebar_layout.addWidget(self.app_list)

        self.btn_remove_app = QPushButton(QIcon.fromTheme("list-remove"), _("Remove"))
        self.btn_remove_app.setEnabled(False)

        self.btn_remove_app.clicked.connect(self._on_remove_app)

        self.sidebar_layout.addWidget(self.btn_remove_app)

        self.layout.addWidget(self.sidebar_widget)

        # Right Content Area
        self.content_widget = QScrollArea()
        self.content_widget.setWidgetResizable(True)
        self.content_widget.setFrameShape(QFrame.NoFrame)

        self.main_container = ScrollContent()
        self.main_layout = QVBoxLayout(self.main_container)
        self.main_layout.setContentsMargins(30, 20, 30, 30)
        self.main_layout.setSpacing(20)

        title = QLabel(_("Applications"))
        title.setObjectName("CategoryTitle")
        self.main_layout.addWidget(title)

        # 1. Global Mode Section (Simplified Card)
        self.global_card = CardWidget("")
        global_layout = QHBoxLayout()
        global_layout.addWidget(QLabel(_("Global Default Mode:")))
        self.combo_global_mode = QComboBox()
        for m in SELECTABLE_MODES:
            self.combo_global_mode.addItem(_(MODE_INFO[m]["title"]), MODE_INFO[m]["title"])

        self.combo_global_mode.currentIndexChanged.connect(self._on_global_mode_changed)
        global_layout.addWidget(self.combo_global_mode)
        self.global_card.content_layout.addLayout(global_layout)

        delay_grid = QGridLayout()
        delay_grid.addWidget(QLabel(_("Default Commit Interval") + ":"), 0, 0)
        delay_grid.addWidget(QLabel(_("Default Backspace Interval") + ":"), 1, 0)
        delay_grid.addWidget(QLabel(_("Default Post Delay") + ":"), 2, 0)
        for row, (key, value) in enumerate(DEFAULT_DELAY_KEYS.items()):
            spin = QSpinBox()
            spin.setRange(1, 1000)
            spin.setSuffix(" ms")
            spin.setValue(value)
            spin.valueChanged.connect(lambda value, k=key: self._on_default_delay_changed(k, value))
            self.default_delay_spins[key] = spin
            delay_grid.addWidget(spin, row, 1)
        self.global_card.content_layout.addLayout(delay_grid)

        self.main_layout.addWidget(self.global_card)

        # 2. Selected App Card (Empty Title)
        self.app_settings_card = CardWidget("")
        self.app_settings_layout = QVBoxLayout()

        # App Info Header
        self.app_header_layout = QHBoxLayout()
        self.app_icon_label = QLabel()
        self.app_icon_label.setFixedSize(48, 48)
        self.app_name_label = QLabel(_("Select an application"))
        self.app_name_label.setStyleSheet("font-size: 16px; font-weight: bold;")
        self.app_header_layout.addWidget(self.app_icon_label)
        self.app_header_layout.addWidget(self.app_name_label)
        self.app_header_layout.addStretch()
        self.app_settings_layout.addLayout(self.app_header_layout)
        self.app_settings_layout.addSpacing(10)

        # Grid for App Modes
        self.mode_grid = QGridLayout()
        self.mode_grid.setSpacing(10)
        self.mode_cards = {}

        grid_modes = [*SELECTABLE_MODES, MODE_DEFAULT]
        for i, m in enumerate(grid_modes):
            card = ModeCard(m)
            card.clicked.connect(self._on_app_mode_changed)
            self.mode_cards[m] = card
            self.mode_grid.addWidget(card, i // 2, i % 2)

        self.app_settings_layout.addLayout(self.mode_grid)

        app_delay_grid = QGridLayout()
        app_delay_grid.addWidget(QLabel(_("Commit Interval") + ":"), 0, 0)
        app_delay_grid.addWidget(QLabel(_("Backspace Interval") + ":"), 1, 0)
        app_delay_grid.addWidget(QLabel(_("Post Delay") + ":"), 2, 0)
        self.spin_app_commit = self._make_app_delay_spin("commit", app_delay_grid, 0)
        self.spin_app_backspace = self._make_app_delay_spin("backspace", app_delay_grid, 1)
        self.spin_app_post = self._make_app_delay_spin("post", app_delay_grid, 2)
        self.app_settings_layout.addLayout(app_delay_grid)

        self.app_settings_card.content_layout.addLayout(self.app_settings_layout)
        self.main_layout.addWidget(self.app_settings_card)
        self.main_layout.addStretch()

        # Initial visibility
        self.app_settings_card.setVisible(False)

        self.content_widget.setWidget(self.main_container)
        self.layout.addWidget(self.content_widget)

    def load_data(self):
        """Loads rules from config and global mode from DBus."""
        self.app_rules = {}
        try:
            data = self.dbus.get_sub_config_list("app_rules", "Rules")
            self.app_rules = parse_app_rules(data)
        except Exception as e:
            print(f"Error loading app rules via DBus: {e}")

        # Sync Global Mode
        config = self.dbus.get_config()
        values = config.get("values", {}) if config else {}
        mode_str = values.get("Mode", "Uinput (Smooth)")
        self.combo_global_mode.blockSignals(True)
        idx = self.combo_global_mode.findData(mode_str)
        if idx >= 0:
            self.combo_global_mode.setCurrentIndex(idx)
        self.combo_global_mode.blockSignals(False)
        self.original_global_mode = mode_str

        # Sync Global Default Delays
        for key, spin in self.default_delay_spins.items():
            spin.blockSignals(True)
            spin.setValue(
                coerce_int(values.get(key, DEFAULT_DELAY_KEYS[key]), DEFAULT_DELAY_KEYS[key])
            )
            spin.blockSignals(False)
        self.original_default_delays = {
            key: str(spin.value()) for key, spin in self.default_delay_spins.items()
        }
        self.modified_default_delays = {}

        self._populate_app_list()
        self.original_app_rules = copy.deepcopy(self.app_rules)

    def _populate_app_list(self):
        self.app_list.clear()
        self._scan_desktop_files()
        apps_to_show = set(self.app_rules.keys())
        if self.selected_app:
            apps_to_show.add(self.selected_app)

        for app in sorted(apps_to_show):
            mode = self.app_rules.get(app, {}).get("mode", MODE_DEFAULT)
            mode_text = _(MODE_INFO.get(mode, MODE_INFO[MODE_SMOOTH])["title"])

            item = QListWidgetItem()
            item.setText(f"{app}\n{mode_text}")
            item.setData(Qt.UserRole, (app, mode))
            item.setIcon(self._resolve_icon(app))
            self.app_list.addItem(item)

            if app == self.selected_app:
                self.app_list.setCurrentItem(item)

    def _scan_desktop_files(self):
        """Builds a robust map of app identifiers to icon names."""
        scan_desktop_icons(
            desktop_search_paths(os.environ, os.path.expanduser("~")), self._icon_cache
        )

    def _resolve_icon(self, app_name):
        """Resolves icon name for a given app handle."""
        return QIcon.fromTheme(
            resolve_icon_name(app_name, self._icon_cache),
            QIcon.fromTheme("application-x-executable"),
        )

    def _filter_apps(self, text):
        for i in range(self.app_list.count()):
            item = self.app_list.item(i)
            item.setHidden(not app_matches_filter(text, item.text()))

    def _on_app_selected(self, item):
        app_name, mode = item.data(Qt.UserRole)
        self.selected_app = app_name
        self.current_app_mode = mode

        self.app_name_label.setText(app_name)
        self.app_icon_label.setPixmap(self._resolve_icon(app_name).pixmap(48, 48))
        self.app_settings_card.setVisible(True)
        self.btn_remove_app.setEnabled(True)  # Enable Remove
        self._sync_app_delay_spins()
        self._update_mode_cards()

    def _on_global_mode_changed(self, index):
        if not self.isVisible():
            return

        self._notify_changed()

    def _on_app_mode_changed(self, mode):
        self.current_app_mode = mode
        apply_app_mode(self.app_rules, self.selected_app, mode)

        self._sync_app_delay_spins()
        self._update_mode_cards()
        self._populate_app_list()
        self._notify_changed()

    def _update_mode_cards(self):
        for m, card in self.mode_cards.items():
            card.selected = m == self.current_app_mode
            card.update_style()

    def _make_app_delay_spin(self, field: str, grid: QGridLayout, row: int) -> QSpinBox:
        spin = QSpinBox()
        spin.setRange(0, 1000)
        spin.setSuffix(" ms")
        spin.setSpecialValueText(_("Default"))
        spin.setEnabled(False)
        spin.valueChanged.connect(lambda value, f=field: self._on_app_delay_changed(f, value))
        grid.addWidget(spin, row, 1)
        return spin

    def _sync_app_delay_spins(self):
        rule = self.app_rules.get(self.selected_app) if self.selected_app else None
        for field, spin in (
            ("commit", self.spin_app_commit),
            ("backspace", self.spin_app_backspace),
            ("post", self.spin_app_post),
        ):
            spin.blockSignals(True)
            spin.setValue(rule_delay(rule, field) if rule else 0)
            spin.blockSignals(False)
            spin.setEnabled(rule is not None)

    def _on_default_delay_changed(self, key: str, value: int):
        self.modified_default_delays[key] = str(value)
        self._notify_changed()

    def _on_app_delay_changed(self, field: str, value: int):
        if self.selected_app is None:
            return
        rule = self.app_rules.get(self.selected_app)
        if rule is None:
            return
        rule[field] = value
        self._notify_changed()

    def _on_remove_app(self):
        if not self.selected_app:
            return

        reply = QMessageBox.question(
            self,
            _("Confirm Remove"),
            _("Remove rules for this application?"),
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply == QMessageBox.No:
            return

        self.app_rules.pop(self.selected_app, None)

        self.selected_app = None
        self.app_settings_card.setVisible(False)
        self.btn_remove_app.setEnabled(False)
        self._populate_app_list()
        self._notify_changed()

    def _notify_changed(self):
        notify_parent_changed(self)

    def is_modified(self):
        """Returns True if the current state differs from the initial loaded state."""
        return (
            self.app_rules != self.original_app_rules
            or self.combo_global_mode.currentData() != self.original_global_mode
            or bool(self.modified_default_delays)
        )

    def is_modified_from_default(self):
        """Returns True if the current state differs from the default state."""
        return (
            len(self.app_rules) > 0
            or self.combo_global_mode.currentData() != "Uinput (Smooth)"
            or any(
                spin.value() != DEFAULT_DELAY_KEYS[key]
                for key, spin in self.default_delay_spins.items()
            )
        )

    def save_data(self) -> bool:
        try:
            mode_changed = self.combo_global_mode.currentData() != self.original_global_mode
            if mode_changed or self.modified_default_delays:
                updates = dict(self.modified_default_delays)
                if mode_changed:
                    updates["Mode"] = self.combo_global_mode.currentData()
                if not self.dbus.save_values(updates):
                    return False

            if not self.dbus.set_sub_config_list(
                "app_rules", "Rules", serialize_app_rules(self.app_rules)
            ):
                return False

            self.original_app_rules = copy.deepcopy(self.app_rules)
            self.original_global_mode = self.combo_global_mode.currentData()
            self.original_default_delays = {
                key: str(spin.value()) for key, spin in self.default_delay_spins.items()
            }
            self.modified_default_delays = {}
            return True

        except Exception as e:
            print(f"Error saving app rules via DBus: {e}")
            return False

    def restore_defaults(self):
        self.load_data()
