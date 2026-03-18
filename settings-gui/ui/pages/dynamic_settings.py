# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Dynamic Settings Page with Card-based Layout matching modern guidelines.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QCheckBox,
    QLabel,
    QScrollArea,
    QFrame,
    QRadioButton,
    QComboBox,
    QButtonGroup,
    QGridLayout,
    QSizePolicy,
)
from ui.components import HotkeyCaptureWidget
from core.dbus_handler import LotusDBusHandler
from enum import Enum
from i18n import _


class SettingsCategory(Enum):
    GENERAL = "general"
    APPEARANCE = "appearance"
    TYPING = "typing"
    SHORTCUTS = "shortcuts"
    INTERFACE = "interface"


# Mapping of settings keys to categories and groups
SETTINGS_MAP = {
    SettingsCategory.GENERAL: {
        "HOTKEYS": ["ModeMenuKey"],
        "INPUT METHOD": ["InputMethod", "Mode", "OutputCharset"],
    },
    SettingsCategory.APPEARANCE: {
        "THEME & ICONS": ["UseLotusIcons"],
    },
    SettingsCategory.TYPING: {
        "SPELLING & CORRECTIONS": ["SpellCheck", "AutoNonVnRestore", "DdFreeStyle"],
        "TYPING OPTIONS": ["ModernStyle", "FreeMarking", "FixUinputWithAck"],
    },
    SettingsCategory.SHORTCUTS: {
        "SHORTCUTS": ["ModeMenuKey"],
    }
}


class CardWidget(QFrame):
    """A visual container (Card) for grouping related settings."""

    def __init__(self, title: str, parent=None):
        super().__init__(parent)
        self.setObjectName("SettingCard")

        self.main_layout = QVBoxLayout(self)
        self.main_layout.setContentsMargins(16, 16, 16, 16)
        self.main_layout.setSpacing(12)

        if title:
            title_label = QLabel(title)
            title_label.setObjectName("CardTitle")
            self.main_layout.addWidget(title_label)

        self.content_layout = QVBoxLayout()
        self.content_layout.setSpacing(10)
        self.main_layout.addLayout(self.content_layout)


class DynamicSettingsPage(QWidget):
    def __init__(self, dbus_handler: LotusDBusHandler, category: SettingsCategory = SettingsCategory.GENERAL, parent=None):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.category = category
        self.current_values = {}
        self.button_groups = []

        self._setup_ui()
        self.load_config()

    def _setup_ui(self):
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)

        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.scroll.setFrameShape(QFrame.NoFrame)

        self.container = QWidget()
        self.container_layout = QVBoxLayout(self.container)
        self.container_layout.setContentsMargins(30, 20, 30, 20)
        self.container_layout.setSpacing(20)

        self.scroll.setWidget(self.container)
        self.layout.addWidget(self.scroll)

    def load_config(self):
        self.blockSignals(True)
        try:
            while self.container_layout.count():
                item = self.container_layout.takeAt(0)
                if item.widget():
                    item.widget().deleteLater()
            self.button_groups.clear()

            config_data = self.dbus.get_config()
            if not config_data:
                self.container_layout.addWidget(QLabel(_("Failed to load configuration.")))
                return

            self.current_values = config_data.get("values", {})
            metadata_list = config_data.get("metadata", [])
            if not metadata_list:
                return

            # Flat map all items for easy lookup
            self.all_metadata = {}
            for group in metadata_list:
                for item in group[1]:
                    self.all_metadata[item[0]] = item

            # Render based on SETTINGS_MAP
            title_text = self.category.name.capitalize()
            title = QLabel(_(title_text))
            title.setObjectName("CategoryTitle")
            self.container_layout.addWidget(title)

            category_groups = SETTINGS_MAP.get(self.category, {})
            for group_name, keys in category_groups.items():
                # Convert ALL CAPS to Title Case
                header_text = group_name.title() if group_name.isupper() else group_name
                header = QLabel(_(header_text))
                header.setObjectName("GroupHeader")
                self.container_layout.addWidget(header)
                
                card = CardWidget("")
                found_any = False
                for k in keys:
                    item = self.all_metadata.get(k)
                    if not item:
                        continue
                    
                    found_any = True
                    type_str = item[1]
                    if k == "ModeMenuKey" or type_str == "Hotkey":
                        self._render_hotkey(item, card.content_layout)
                    elif "Enum" in item[4]:
                        self._render_combobox(item, card.content_layout)
                    elif type_str == "Boolean":
                        self._render_checkbox(item, card.content_layout)
                
                if found_any:
                    self.container_layout.addWidget(card)

            if self.category == SettingsCategory.INTERFACE and not category_groups:
                self.container_layout.addWidget(QLabel(_("No interface settings available yet.")))

            self.container_layout.addStretch()
        finally:
            self.blockSignals(False)

    def is_modified_from_default(self):
        if not hasattr(self, "all_metadata"):
            return False
        for key, val in self.current_values.items():
            meta = self.all_metadata.get(key)
            if meta:
                default_val = meta[3]
                # Handle cases where default_val might be a dict (like hotkeys)
                if isinstance(default_val, dict) and isinstance(val, dict):
                    if str(val.get("0")) != str(default_val.get("0")):
                        return True
                elif str(val) != str(default_val):
                    return True
        return False

    def _render_hotkey(self, item, layout):
        key, type_str, label, default, annotations = item
        val = self.current_values.get(key, default)

        hotkey_str = val.get("0", "") if isinstance(val, dict) else ""

        row_layout = QHBoxLayout()
        row_layout.addWidget(QLabel(_(label)))
        row_layout.addStretch()

        hk_btn = HotkeyCaptureWidget(hotkey_str)
        hk_btn.setFixedWidth(200)
        hk_btn.textChanged.connect(
            lambda text, k=key: self.update_config(k, {"0": text})
        )

        row_layout.addWidget(hk_btn)
        layout.addLayout(row_layout)

    def _render_combobox(self, item, layout):
        key, type_str, label, default, annotations = item
        val = str(self.current_values.get(key, default))

        if "Enum" not in annotations:
            return

        row_layout = QHBoxLayout()
        row_layout.addWidget(QLabel(_(label)))
        row_layout.addStretch()

        combo = QComboBox()
        combo.setFixedWidth(200)
        enum_dict = annotations.get("Enum", {})
        sorted_keys = sorted(
            enum_dict.keys(), key=lambda x: int(x) if str(x).isdigit() else x
        )

        for k in sorted_keys:
            rb_text = str(enum_dict[k])
            combo.addItem(_(rb_text), rb_text)

        idx = combo.findData(val)
        if idx >= 0:
            combo.setCurrentIndex(idx)

        combo.currentTextChanged.connect(
            lambda text, k=key: self.update_config(k, combo.currentData())
        )
        row_layout.addWidget(combo)
        layout.addLayout(row_layout)

    def _render_radio_group(self, item, layout, columns=1):
        key, type_str, label, default, annotations = item
        val = str(self.current_values.get(key, default))

        if "Enum" not in annotations:
            return

        subtitle = QLabel(f"<b>{_(label)}</b>")
        if label != "Output Charset":
            layout.addWidget(subtitle)

        enum_dict = annotations.get("Enum", {})
        sorted_keys = sorted(
            enum_dict.keys(), key=lambda x: int(x) if str(x).isdigit() else x
        )

        btn_group = QButtonGroup(self)
        self.button_groups.append(btn_group)

        grid = QGridLayout()
        grid.setHorizontalSpacing(40)
        grid.setVerticalSpacing(8)

        row, col = 0, 0
        for k in sorted_keys:
            rb_text = str(enum_dict[k])
            rb = QRadioButton(_(rb_text))

            rb.setProperty("val_str", rb_text)

            if rb_text == val:
                rb.setChecked(True)

            btn_group.addButton(rb)
            grid.addWidget(rb, row, col)

            col += 1
            if col >= columns:
                col = 0
                row += 1

        btn_group.buttonClicked.connect(
            lambda btn, k=key: self.update_config(k, btn.property("val_str"))
        )
        layout.addLayout(grid)

    def _render_checkbox(self, item, layout):
        key, type_str, label, default, annotations = item
        val = self.current_values.get(key, default)

        cb = QCheckBox(_(label))
        is_checked = str(val).lower() == "true"
        cb.setChecked(is_checked)

        cb.toggled.connect(
            lambda checked, k=key: self.update_config(k, "True" if checked else "False")
        )
        layout.addWidget(cb)

    def restore_defaults(self):
        """Resets current values to engine defaults."""
        self.blockSignals(True)
        try:
            config_data = self.dbus.get_config()
            if not config_data:
                return

            metadata_list = config_data.get("metadata", [])
            new_values = {}
            for group in metadata_list:
                for item in group[1]:
                    key, type_str, label, default, annotations = item
                    new_values[key] = default
            
            self.current_values = new_values
            self.load_config()
        finally:
            self.blockSignals(False)

    def save_data(self, quiet=False):
        """Commits all staged changes to DBus."""
        if self.current_values:
            self.dbus.set_config(self.current_values)

    def update_config(self, key: str, new_value):
        """Updates internal state and notifies parent window of change."""
        self.current_values[key] = new_value
        # Notify the parent window (LotusSettingsWindow) if it exists
        main_win = self.window()
        if hasattr(main_win, "on_changed"):
            main_win.on_changed()
