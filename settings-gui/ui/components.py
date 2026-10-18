# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Reusable UI components.
Uses system's libxkbcommon to natively resolve XKB keysym names,
convert keysyms to Unicode, and mathematically handle Shift modifiers.
"""

import ctypes
import ctypes.util

from core.hotkey_display import (
    HOTKEY_SYM_MAP,
    HOTKEY_UI_UNSHIFT_MAP,
    pretty_format_hotkey_parts,
)
from i18n import _
from qtpy.QtCore import QEvent, Qt, Signal
from qtpy.QtGui import QIcon, QKeySequence
from qtpy.QtWidgets import QHBoxLayout, QLabel, QPushButton, QWidget

libxkb = None
libxkb_path = ctypes.util.find_library("xkbcommon")
if libxkb_path:
    try:
        libxkb = ctypes.CDLL(libxkb_path)

        libxkb.xkb_keysym_get_name.argtypes = [
            ctypes.c_uint32,
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        libxkb.xkb_keysym_get_name.restype = ctypes.c_int

        libxkb.xkb_keysym_to_lower.argtypes = [ctypes.c_uint32]
        libxkb.xkb_keysym_to_lower.restype = ctypes.c_uint32

        libxkb.xkb_keysym_to_utf32.argtypes = [ctypes.c_uint32]
        libxkb.xkb_keysym_to_utf32.restype = ctypes.c_uint32

    except Exception as e:
        print(f"Failed to load libxkbcommon: {e}")


# Re-exported for the widgets below and for callers importing them from here.


def clear_layout(layout) -> None:
    """Removes and deletes every widget owned by the layout."""
    while layout.count():
        item = layout.takeAt(0)
        if item.widget():
            item.widget().deleteLater()


class KeyCap(QLabel):
    """A label styled as a keyboard keycap."""

    def __init__(self, text, parent=None):
        super().__init__(text, parent)
        self.setAlignment(Qt.AlignCenter)
        self.setObjectName("KeyCap")


class HelpIcon(QLabel):
    """A reusable help icon with a tooltip."""

    def __init__(self, text="", parent=None):
        super().__init__(parent)
        # Try symbolic icon first for monochrome look, fall back to standard
        icon = QIcon.fromTheme("help-about-symbolic")
        if icon.isNull():
            icon = QIcon.fromTheme("help-about")

        self.setPixmap(icon.pixmap(32, 32))
        self.setScaledContents(True)
        if text:
            self.setToolTip(text)
        self.setFixedSize(16, 16)


class HotkeyEditorWidget(QWidget):
    """A widget containing HotkeyCaptureWidget with a clear button."""

    textChanged = Signal(str)

    def __init__(self, current_key="", parent=None):
        super().__init__(parent)
        self._setup_ui(current_key)

    def _setup_ui(self, current_key):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        self.hotkey_capture = HotkeyCaptureWidget(current_key)
        self.hotkey_capture.textChanged.connect(self._on_hotkey_changed)

        self.btn_clear = QPushButton(QIcon.fromTheme("delete"), "")
        self.btn_clear.setFixedWidth(30)
        self.btn_clear.setToolTip(_("Clear hotkey"))
        self.btn_clear.clicked.connect(self._on_clear)

        layout.addWidget(self.hotkey_capture)
        layout.addWidget(self.btn_clear)

    def _on_hotkey_changed(self, text):
        self.textChanged.emit(text)

    def _on_clear(self):
        self.hotkey_capture.current_key = ""
        self.hotkey_capture.setChecked(False)
        self.hotkey_capture._update_display()
        self.textChanged.emit("")


class HotkeyCaptureWidget(QPushButton):
    """A button that captures keystrokes to set an Fcitx5-compatible hotkey.
    Provides live visual feedback for held modifiers during recording."""

    textChanged = Signal(str)

    def __init__(self, current_key="", parent=None):
        super().__init__(parent)
        self.current_key = current_key
        self.setCheckable(True)
        self.setObjectName("HotkeyButton")

        # Local state to track held modifiers during recording
        self.record_mods = set()

        # Use a layout for visual keycaps
        self.main_layout = QHBoxLayout(self)
        self.main_layout.setContentsMargins(8, 2, 8, 2)
        self.main_layout.setSpacing(4)
        self.main_layout.setAlignment(Qt.AlignCenter)

        self.toggled.connect(self._on_toggled)
        self._update_display()

        # Install event filter to catch KeyPress, KeyRelease and ShortcutOverride
        self.installEventFilter(self)

    def eventFilter(self, obj, event):
        # Prevent mnemonics and navigation from triggering while we are recording a hotkey
        if obj == self and self.isChecked():
            if event.type() == QEvent.ShortcutOverride:
                event.accept()
                return True
            if event.type() == QEvent.KeyPress:
                self._handle_key_event(event)
                return True
            if event.type() == QEvent.KeyRelease:
                self._handle_release_event(event)
                return True
        return super().eventFilter(obj, event)

    def _on_toggled(self, checked):
        if checked:
            self.record_mods.clear()
        self._update_display()

    def _clear_layout(self):
        clear_layout(self.main_layout)

    def _update_display(self):
        self._clear_layout()

        if self.isChecked():
            if not self.record_mods:
                lbl = QLabel(_("[ Recording... ]"))
                lbl.setStyleSheet("color: palette(highlight); font-weight: bold;")
                self.main_layout.addWidget(lbl)
            else:
                # Show current active modifiers
                mod_order = ["Control", "Alt", "Super", "Shift"]
                for mod in mod_order:
                    if mod in self.record_mods:
                        self.main_layout.addWidget(KeyCap(HOTKEY_SYM_MAP.get(mod, mod)))
                # Add a pulsing placeholder for the next key
                lbl = QLabel("...")
                lbl.setStyleSheet("color: palette(highlight);")
                self.main_layout.addWidget(lbl)
        elif not self.current_key:
            lbl = QLabel(_("None"))
            lbl.setStyleSheet("color: palette(mid);")
            self.main_layout.addWidget(lbl)
        else:
            parts = pretty_format_hotkey_parts(self.current_key)
            for part in parts:
                cap = KeyCap(part)
                self.main_layout.addWidget(cap)

    def keyPressEvent(self, event):
        """Standard key event handler, only active when not recording."""
        if not self.isChecked():
            super().keyPressEvent(event)
            return

    def _handle_key_event(self, event):
        """Internal helper to process captured keys."""
        key_code = event.key()
        modifiers = event.modifiers()

        # Escape cancels the recording
        if key_code == Qt.Key_Escape:
            self.setChecked(False)
            return

        # Track modifiers in real-time
        mod_map = {
            Qt.Key_Control: "Control",
            Qt.Key_Shift: "Shift",
            Qt.Key_Alt: "Alt",
            Qt.Key_Meta: "Super",
        }
        if key_code in mod_map:
            self.record_mods.add(mod_map[key_code])
            self._update_display()
            return

        if key_code == Qt.Key_unknown:
            return

        # It's a base key! Capture it and finalize immediately
        keysym = event.nativeVirtualKey()
        base_key = ""

        if libxkb and keysym > 0:
            # Use raw keysym name to ensure compatibility with Fcitx5 engine
            buf = ctypes.create_string_buffer(64)
            if libxkb.xkb_keysym_get_name(keysym, buf, 64) > 0:
                base_key = buf.value.decode("utf-8")
                # Normalize BackTab keysym to Tab to ensure Fcitx5 sees 'Shift+Tab' instead of plain 'ISO_Left_Tab'
                if base_key == "ISO_Left_Tab":
                    base_key = "Tab"

        if not base_key:
            base_key = (
                event.text()
                if event.text() and event.text().isprintable()
                else QKeySequence(key_code).toString()
            )

        has_non_shift_modifier = bool(
            modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        )
        is_ascii_alpha = len(base_key) == 1 and base_key.isascii() and base_key.isalpha()

        if has_non_shift_modifier and is_ascii_alpha:
            base_key = base_key.upper()

        # Build modifier list from current event state
        mods = []
        if modifiers & Qt.ControlModifier:
            mods.append("Control")
        if modifiers & Qt.AltModifier:
            mods.append("Alt")
        if modifiers & Qt.MetaModifier:
            mods.append("Super")

        # Selective Shift suppression for symbols
        if (modifiers & Qt.ShiftModifier) and (
            (has_non_shift_modifier and is_ascii_alpha) or (base_key not in HOTKEY_UI_UNSHIFT_MAP)
        ):
            mods.append("Shift")

        mods.append(base_key)
        self.current_key = "+".join(mods)
        self.setChecked(False)
        self.textChanged.emit(self.current_key)

    def _handle_release_event(self, event):
        """Processes key release to update live feedback."""
        if not self.isChecked():
            return

        key_code = event.key()
        mod_map = {
            Qt.Key_Control: "Control",
            Qt.Key_Shift: "Shift",
            Qt.Key_Alt: "Alt",
            Qt.Key_Meta: "Super",
        }
        if key_code in mod_map:
            val = mod_map[key_code]
            if val in self.record_mods:
                self.record_mods.remove(val)
                self._update_display()


class SingleKeyCaptureWidget(HotkeyCaptureWidget):
    """A version of HotkeyCaptureWidget that only captures a single key (no modifiers)."""

    def _update_display(self):
        self._clear_layout()

        if self.isChecked():
            lbl = QLabel(_("[ Press Key... ]"))
            lbl.setStyleSheet("color: palette(highlight); font-weight: bold;")
            self.main_layout.addWidget(lbl)
        elif not self.current_key:
            lbl = QLabel(_("None"))
            lbl.setStyleSheet("color: palette(mid);")
            self.main_layout.addWidget(lbl)
        else:
            # For single key, we just show it directly or via map
            display_text = HOTKEY_SYM_MAP.get(self.current_key, self.current_key.capitalize())
            cap = KeyCap(display_text)
            self.main_layout.addWidget(cap)

    def _handle_key_event(self, event):
        key_code = event.key()

        # Escape cancels
        if key_code == Qt.Key_Escape:
            self.setChecked(False)
            return

        # Ignore standalone modifier keys
        if key_code in (Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta):
            return

        if key_code == Qt.Key_unknown:
            return

        # Capture base key only
        keysym = event.nativeVirtualKey()
        base_key = ""

        if libxkb and keysym > 0:
            buf = ctypes.create_string_buffer(64)
            if libxkb.xkb_keysym_get_name(keysym, buf, 64) > 0:
                base_key = buf.value.decode("utf-8")

        if not base_key:
            base_key = (
                event.text()
                if event.text() and event.text().isprintable()
                else QKeySequence(key_code).toString()
            )

        if base_key:
            self.current_key = base_key
            self.setChecked(False)
            self.textChanged.emit(self.current_key)


class ScrollContent(QWidget):
    """Content widget for a page QScrollArea."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._syncing = False

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self._syncing:
            return
        self._syncing = True
        try:
            self._sync_heights()
        finally:
            self._syncing = False

    def _sync_heights(self):
        for label in self.findChildren(QLabel):
            if not label.wordWrap() or not label.text() or not label.isVisibleTo(self):
                continue
            needed = label.heightForWidth(label.width())
            if needed > 0:
                label.setMinimumHeight(needed)
        layout = self.layout()
        if layout is None:
            return
        layout.activate()
        needed = layout.totalHeightForWidth(self.width())
        if needed > 0:
            self.setMinimumHeight(needed)
