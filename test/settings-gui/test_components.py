# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for ui.components hotkey formatting and capture widgets."""

import unittest
from unittest import mock

import _stubs  # noqa: F401  (installs the qtpy/dbus stand-ins)
from qtpy.QtCore import QEvent, Qt
from ui.components import (
    HOTKEY_SYM_MAP,
    HOTKEY_UI_UNSHIFT_MAP,
    HotkeyCaptureWidget,
    HotkeyEditorWidget,
    SingleKeyCaptureWidget,
    clear_layout,
    pretty_format_hotkey_parts,
)


def _key_event(key=None, text="", modifiers=0, virtual_key=0):
    """Builds a fake QKeyEvent for the capture-widget handlers."""
    event = mock.MagicMock(name="QKeyEvent")
    if key is not None:
        event.key.return_value = key
    event.text.return_value = text
    event.modifiers.return_value = modifiers
    event.nativeVirtualKey.return_value = virtual_key
    return event


class PrettyFormatHotkeyPartsTests(unittest.TestCase):
    def test_formatting_cases(self):
        cases = [
            # (input, expected display parts)
            ("", []),
            ("+", ["+"]),
            ("Control++", ["Ctrl", "+"]),
            ("a", ["A"]),
            ("A", ["Shift", "A"]),
            ("Control+a", ["Ctrl", "A"]),
            ("Control+A", ["Ctrl", "A"]),
            ("f5", ["F5"]),
            ("asciitilde", ["Shift", "`"]),
            ("Shift+asciitilde", ["Shift", "`"]),
            ("exclam", ["Shift", "1"]),
            ("Control+plus", ["Ctrl", "Shift", "="]),
            ("ISO_Left_Tab", ["Tab"]),
            ("Shift+ISO_Left_Tab", ["Shift", "Tab"]),
            ("Delete", ["Del"]),
            ("Control+Shift_L", ["Ctrl", "L-Shift"]),
            ("Shift+Shift_L", ["L-Shift"]),
            ("Super+Meta_R", ["R-Super"]),
            ("Alt+Control+x", ["Ctrl", "Alt", "X"]),
            ("Control+Control+a", ["Ctrl", "A"]),
        ]
        for hotkey, expected in cases:
            with self.subTest(hotkey=hotkey):
                self.assertEqual(pretty_format_hotkey_parts(hotkey), expected)

    def test_modifier_order_is_canonical(self):
        self.assertEqual(
            pretty_format_hotkey_parts("Shift+Super+Control+a"),
            ["Ctrl", "Super", "Shift", "A"],
        )

    def test_none_like_empty_input(self):
        self.assertEqual(pretty_format_hotkey_parts(None), [])


class HotkeyMapInvariantTests(unittest.TestCase):
    def test_unshift_targets_are_displayable(self):
        for shifted, base in HOTKEY_UI_UNSHIFT_MAP.items():
            with self.subTest(shifted=shifted, base=base):
                self.assertTrue(len(base) == 1 or base in HOTKEY_SYM_MAP)

    def test_symbol_map_values_are_non_empty(self):
        for keysym, label in HOTKEY_SYM_MAP.items():
            with self.subTest(keysym=keysym):
                self.assertIsInstance(label, str)
                self.assertTrue(label)


class ClearLayoutTests(unittest.TestCase):
    def test_deletes_every_widget(self):
        from qtpy.QtWidgets import QHBoxLayout, QLabel

        layout = QHBoxLayout()
        first = QLabel()
        second = QLabel()
        layout.addWidget(first)
        layout.addWidget(second)

        clear_layout(layout)

        self.assertEqual(layout.count(), 0)
        first.deleteLater.assert_called_once()
        second.deleteLater.assert_called_once()

    def test_tolerates_items_without_widgets(self):
        from qtpy.QtWidgets import QHBoxLayout

        layout = QHBoxLayout()
        layout.addWidget(None)
        layout.addWidget(mock.MagicMock(name="non-widget-item"))

        clear_layout(layout)

        self.assertEqual(layout.count(), 0)


class HotkeyCaptureWidgetTests(unittest.TestCase):
    def test_initial_key_is_kept(self):
        widget = HotkeyCaptureWidget("Control+a")
        self.assertEqual(widget.current_key, "Control+a")

    def test_capture_plain_letter(self):
        widget = HotkeyCaptureWidget("Control+a")
        widget._handle_key_event(_key_event(key=Qt.Key_A, text="a"))

        self.assertEqual(widget.current_key, "a")
        widget.textChanged.emit.assert_called_once_with("a")

    def test_control_uppercases_letter(self):
        widget = HotkeyCaptureWidget()
        widget._handle_key_event(_key_event(key=Qt.Key_A, text="a", modifiers=Qt.ControlModifier))

        self.assertEqual(widget.current_key, "Control+A")

    def test_shift_symbol_is_kept_with_shift(self):
        widget = HotkeyCaptureWidget()
        widget._handle_key_event(_key_event(key=Qt.Key_1, text="!", modifiers=Qt.ShiftModifier))

        self.assertEqual(widget.current_key, "Shift+!")

    def test_escape_cancels_recording(self):
        widget = HotkeyCaptureWidget("Control+a")
        widget.setChecked.reset_mock()
        widget._handle_key_event(_key_event(key=Qt.Key_Escape))

        self.assertEqual(widget.current_key, "Control+a")
        widget.setChecked.assert_called_once_with(False)

    def test_unknown_key_is_ignored(self):
        widget = HotkeyCaptureWidget("Control+a")
        widget._handle_key_event(_key_event(key=Qt.Key_unknown))

        self.assertEqual(widget.current_key, "Control+a")

    def test_modifier_press_and_release_tracking(self):
        widget = HotkeyCaptureWidget()

        widget._handle_key_event(_key_event(key=Qt.Key_Control))
        self.assertIn("Control", widget.record_mods)

        widget._handle_release_event(_key_event(key=Qt.Key_Control))
        self.assertNotIn("Control", widget.record_mods)

    def test_shift_letter_is_stored_as_uppercase(self):
        # "A" is in HOTKEY_UI_UNSHIFT_MAP, so Shift stays implicit in the stored
        # key and pretty_format_hotkey_parts re-adds the Shift label for display.
        widget = HotkeyCaptureWidget()
        widget._handle_key_event(_key_event(key=Qt.Key_A, text="A", modifiers=Qt.ShiftModifier))

        self.assertEqual(widget.current_key, "A")

    def test_recording_feedback_shows_held_modifiers(self):
        widget = HotkeyCaptureWidget()

        # Driven directly because the stub does not fire toggled from setChecked.
        widget._on_toggled(True)
        self.assertEqual(widget.main_layout.count(), 1)

        widget._handle_key_event(_key_event(key=Qt.Key_Control))
        self.assertEqual(widget.main_layout.count(), 2)

        widget._handle_release_event(_key_event(key=Qt.Key_Control))
        self.assertEqual(widget.main_layout.count(), 1)

    def test_event_filter_handles_key_press(self):
        widget = HotkeyCaptureWidget()
        event = _key_event(key=Qt.Key_B, text="b")
        event.type.return_value = QEvent.KeyPress

        self.assertTrue(widget.eventFilter(widget, event))
        self.assertEqual(widget.current_key, "b")

    def test_event_filter_accepts_shortcut_override(self):
        widget = HotkeyCaptureWidget()
        event = _key_event(key=Qt.Key_A)
        event.type.return_value = QEvent.ShortcutOverride

        self.assertTrue(widget.eventFilter(widget, event))
        event.accept.assert_called_once()


class SingleKeyCaptureWidgetTests(unittest.TestCase):
    def test_capture_plain_letter_only(self):
        widget = SingleKeyCaptureWidget("f5")
        widget._handle_key_event(_key_event(key=Qt.Key_A, text="a"))

        self.assertEqual(widget.current_key, "a")
        widget.textChanged.emit.assert_called_once_with("a")

    def test_standalone_modifier_is_ignored(self):
        widget = SingleKeyCaptureWidget("f5")
        widget._handle_key_event(_key_event(key=Qt.Key_Shift))

        self.assertEqual(widget.current_key, "f5")
        widget.textChanged.emit.assert_not_called()

    def test_escape_cancels(self):
        widget = SingleKeyCaptureWidget("f5")
        widget._handle_key_event(_key_event(key=Qt.Key_Escape))

        self.assertEqual(widget.current_key, "f5")
        widget.setChecked.assert_called_once_with(False)


class HotkeyEditorWidgetTests(unittest.TestCase):
    def test_clear_resets_capture_and_emits_empty(self):
        widget = HotkeyEditorWidget("Control+a")

        widget._on_clear()

        self.assertEqual(widget.hotkey_capture.current_key, "")
        widget.hotkey_capture.setChecked.assert_called_once_with(False)
        widget.textChanged.emit.assert_called_once_with("")


if __name__ == "__main__":
    unittest.main()
