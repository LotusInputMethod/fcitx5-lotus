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


# Common XKB keysym name to character mapping for display
HOTKEY_SYM_MAP = {
    "grave": "`", "minus": "-", "equal": "=", "bracketleft": "[", "bracketright": "]",
    "backslash": "\\", "semicolon": ";", "apostrophe": "'", "comma": ",", "period": ".",
    "slash": "/", "asciitilde": "~", "underscore": "_", "plus": "+",
    "braceleft": "{", "braceright": "}", "bar": "|", "colon": ":", "quotedbl": '"',
    "less": "<", "greater": ">", "question": "?", "exclam": "!", "at": "@",
    "numbersign": "#", "dollar": "$", "percent": "%", "asciicircum": "^",
    "ampersand": "&", "asterisk": "*", "parenleft": "(", "parenright": ")",
    "Control": "Ctrl", "Escape": "Esc", "Return": "Enter", "BackSpace": "Backspace",
    "Delete": "Del", "Insert": "Ins", "ISO_Left_Tab": "Tab",
    "Shift_L": "L-Shift", "Shift_R": "R-Shift",
    "Control_L": "L-Ctrl", "Control_R": "R-Ctrl",
    "Alt_L": "L-Alt", "Alt_R": "R-Alt",
    "Super_L": "L-Super", "Super_R": "R-Super",
    "Meta_L": "L-Super", "Meta_R": "R-Super",
}


# Mapping for UI DISPLAY ONLY: Converts shifted keysyms back to base keys + explicit Shift label.
# This keeps the stored string engine-compliant (e.g. 'asciitilde') but UI-friendly (e.g. 'Shift' + '`').
# NOTE: We exclude ISO_Left_Tab here because Fcitx5 configuration prefers explicit 'Shift+Tab' strings.
HOTKEY_UI_UNSHIFT_MAP = {
    "asciitilde": "grave",
    "exclam": "1", "at": "2", "numbersign": "3", "dollar": "4", "percent": "5",
    "asciicircum": "6", "ampersand": "7", "asterisk": "8", "parenleft": "9", "parenright": "0",
    "underscore": "minus", "plus": "equal", "braceleft": "bracketleft", "braceright": "bracketright",
    "bar": "backslash", "colon": "semicolon", "quotedbl": "apostrophe",
    "less": "comma", "greater": "period", "question": "slash"
}

for char in "abcdefghijklmnopqrstuvwxyz":
    HOTKEY_UI_UNSHIFT_MAP[char.upper()] = char


def pretty_format_hotkey_parts(hotkey_str):
    """Converts internal keysym names to user-friendly characters for display as a list.
    Automatically de-normalizes shifted keysyms and de-duplicates redundant modifiers."""
    if not hotkey_str:
        return []

    # Robustly handle hotkeys containing '+' key (e.g. 'Control++')
    if hotkey_str == "+":
        parts = ["+"]
    elif hotkey_str.endswith("++"):
        parts = hotkey_str[:-2].split("+") + ["+"]
    else:
        parts = hotkey_str.split("+")

    raw_mods = []
    base_key_part = None

    # Analyze parts to identify modifiers and the base key
    for part in parts:
        if not part: continue
        if part in ("Control", "Alt", "Super", "Shift"):
            if part not in raw_mods:
                raw_mods.append(part)
        else:
            base_key_part = part

    # De-duplicate: If base key IS a specific modifier (e.g. Shift_L),
    # suppress the corresponding generic modifier label.
    if base_key_part:
        if base_key_part.startswith("Shift_"):
            if "Shift" in raw_mods: raw_mods.remove("Shift")
        elif base_key_part.startswith("Control_") or base_key_part == "Control":
            if "Control" in raw_mods: raw_mods.remove("Control")
        elif base_key_part.startswith("Alt_") or base_key_part == "Alt":
            if "Alt" in raw_mods: raw_mods.remove("Alt")
        elif base_key_part.startswith("Super_") or base_key_part.startswith("Meta_"):
            if "Super" in raw_mods: raw_mods.remove("Super")

    pretty_parts = []
    explicit_shift_needed = "Shift" in raw_mods
    has_non_shift_modifier = any(mod in raw_mods for mod in ("Control", "Alt", "Super"))

    # Process base key for UI display
    if base_key_part:
        if len(base_key_part) == 1 and base_key_part.isupper():
            explicit_shift_needed = explicit_shift_needed or not has_non_shift_modifier
            base_key_label = base_key_part # Keep uppercase A-Z
        # Check for symbols that imply Shift (Display only)
        elif base_key_part in HOTKEY_UI_UNSHIFT_MAP:
            explicit_shift_needed = True
            display_base = HOTKEY_UI_UNSHIFT_MAP[base_key_part]
            base_key_label = HOTKEY_SYM_MAP.get(display_base, display_base.capitalize())
        else:
            base_key_label = HOTKEY_SYM_MAP.get(base_key_part, base_key_part.capitalize())
    else:
        base_key_label = ""

    # Build the final ordered list of labels
    if "Control" in raw_mods: pretty_parts.append("Ctrl")
    if "Alt" in raw_mods: pretty_parts.append("Alt")
    if "Super" in raw_mods: pretty_parts.append("Super")
    if explicit_shift_needed: pretty_parts.append("Shift")

    if base_key_label:
        pretty_parts.append(base_key_label)

    return pretty_parts


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
        while self.main_layout.count():
            item = self.main_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

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
            Qt.Key_Meta: "Super"
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
            base_key = event.text() if event.text() and event.text().isprintable() else QKeySequence(key_code).toString()

        has_non_shift_modifier = bool(
            modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        )
        is_ascii_alpha = (
            len(base_key) == 1 and base_key.isascii() and base_key.isalpha()
        )

        if has_non_shift_modifier and is_ascii_alpha:
            base_key = base_key.upper()

        # Build modifier list from current event state
        mods = []
        if modifiers & Qt.ControlModifier: mods.append("Control")
        if modifiers & Qt.AltModifier: mods.append("Alt")
        if modifiers & Qt.MetaModifier: mods.append("Super")

        # Selective Shift suppression for symbols
        if (modifiers & Qt.ShiftModifier) and (
            (has_non_shift_modifier and is_ascii_alpha)
            or (base_key not in HOTKEY_UI_UNSHIFT_MAP)
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
            Qt.Key_Meta: "Super"
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
            base_key = event.text() if event.text() and event.text().isprintable() else QKeySequence(key_code).toString()

        if base_key:
            self.current_key = base_key
            self.setChecked(False)
            self.textChanged.emit(self.current_key)
