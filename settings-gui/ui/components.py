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
from qtpy.QtWidgets import QPushButton, QLabel, QHBoxLayout
from qtpy.QtGui import QKeySequence
from qtpy.QtCore import Qt, Signal, QEvent
from i18n import _

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


# Common XKB keysym name to character mapping for display
HOTKEY_SYM_MAP = {
    "grave": "`",
    "minus": "-",
    "equal": "=",
    "bracketleft": "[",
    "bracketright": "]",
    "backslash": "\\",
    "semicolon": ";",
    "apostrophe": "'",
    "comma": ",",
    "period": ".",
    "slash": "/",
    "asciitilde": "~",
    "underscore": "_",
    "plus": "+",
    "braceleft": "{",
    "braceright": "}",
    "bar": "|",
    "colon": ":",
    "quotedbl": '"',
    "less": "<",
    "greater": ">",
    "question": "?",
    "exclam": "!",
    "at": "@",
    "numbersign": "#",
    "dollar": "$",
    "percent": "%",
    "asciicircum": "^",
    "ampersand": "&",
    "asterisk": "*",
    "parenleft": "(",
    "parenright": ")",
    "Control": "Ctrl",
    "Escape": "Esc",
    "Return": "Enter",
    "BackSpace": "Backspace",
    "Delete": "Del",
    "Insert": "Ins",
    "ISO_Left_Tab": "Tab",
}

# Mapping to "unshift" symbols back to their base keys when Shift is used (standard US-like layout)
HOTKEY_UNSHIFT_MAP = {
    "exclam": "1", "at": "2", "numbersign": "3", "dollar": "4", "percent": "5",
    "asciicircum": "6", "ampersand": "7", "asterisk": "8", "parenleft": "9", "parenright": "0",
    "underscore": "minus", "plus": "equal", "braceleft": "bracketleft", "braceright": "bracketright",
    "bar": "backslash", "colon": "semicolon", "quotedbl": "apostrophe",
    "less": "comma", "greater": "period", "question": "slash", "asciitilde": "grave",
    "ISO_Left_Tab": "Tab"
}


def pretty_format_hotkey_parts(hotkey_str):
    """Converts internal keysym names to user-friendly characters for display as a list."""
    if not hotkey_str:
        return []

    # Robustly handle hotkeys containing '+' key (e.g. 'Control++')
    if hotkey_str == "+":
        parts = ["+"]
    elif hotkey_str.endswith("++"):
        parts = hotkey_str[:-2].split("+") + ["+"]
    else:
        parts = hotkey_str.split("+")

    pretty_parts = []
    for part in parts:
        if not part:
            continue  # Handles trailing or consecutive '+' from split()
        pretty_parts.append(HOTKEY_SYM_MAP.get(part, part.capitalize()))

    return pretty_parts


class KeyCap(QLabel):
    """A label styled as a keyboard keycap."""
    def __init__(self, text, parent=None):
        super().__init__(text, parent)
        self.setAlignment(Qt.AlignCenter)
        self.setObjectName("KeyCap")


class HotkeyCaptureWidget(QPushButton):
    """A button that captures keystrokes to set an Fcitx5-compatible hotkey."""

    textChanged = Signal(str)

    def __init__(self, current_key="", parent=None):
        super().__init__(parent)
        self.current_key = current_key
        self.setCheckable(True)
        self.setObjectName("HotkeyButton")
        
        # Use a layout for visual keycaps
        self.main_layout = QHBoxLayout(self)
        self.main_layout.setContentsMargins(8, 2, 8, 2)
        self.main_layout.setSpacing(4)
        self.main_layout.setAlignment(Qt.AlignCenter)
        
        self.toggled.connect(self._on_toggled)
        self._update_display()

        # Install event filter to catch ShortcutOverride (used for mnemonics like Alt+O)
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
        return super().eventFilter(obj, event)

    def _on_toggled(self, checked):
        self._update_display()

    def _clear_layout(self):
        while self.main_layout.count():
            item = self.main_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

    def _update_display(self):
        self._clear_layout()
        
        if self.isChecked():
            lbl = QLabel(_("[ Recording... ]"))
            lbl.setStyleSheet("color: palette(highlight); font-weight: bold;")
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
        # Recording events are handled by eventFilter to intercept navigation keys

    def _handle_key_event(self, event):
        """Internal helper to process captured keys."""
        key_code = event.key()

        # Escape cancels the recording
        if key_code == Qt.Key_Escape:
            self.setChecked(False)
            return

        # Ignore standalone modifier presses
        if key_code in (
            Qt.Key_Control,
            Qt.Key_Shift,
            Qt.Key_Alt,
            Qt.Key_Meta,
            Qt.Key_unknown,
        ):
            return

        keysym = event.nativeVirtualKey()
        base_key = ""

        if libxkb and keysym > 0:
            # Always try to use the lower-case/base version of the keysym
            lower_sym = libxkb.xkb_keysym_to_lower(keysym)
            
            buf = ctypes.create_string_buffer(64)
            if libxkb.xkb_keysym_get_name(lower_sym, buf, 64) > 0:
                base_key = buf.value.decode("utf-8")

        if event.modifiers() & Qt.ShiftModifier and base_key in HOTKEY_UNSHIFT_MAP:
            base_key = HOTKEY_UNSHIFT_MAP[base_key]

        if not base_key:
            base_key = event.text() if event.text() and event.text().isprintable() else QKeySequence(key_code).toString()

        mods = []
        if event.modifiers() & Qt.ControlModifier:
            mods.append("Control")
        if event.modifiers() & Qt.AltModifier:
            mods.append("Alt")
        if event.modifiers() & Qt.MetaModifier:
            mods.append("Super")

        if event.modifiers() & Qt.ShiftModifier:
            mods.append("Shift")

        mods.append(base_key)
        self.current_key = "+".join(mods)

        self.setChecked(False)
        self.textChanged.emit(self.current_key)
