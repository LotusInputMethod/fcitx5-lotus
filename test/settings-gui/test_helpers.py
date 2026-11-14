# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for ui.helpers utility functions."""

import os
import unittest
from unittest import mock

import _stubs  # noqa: F401  (installs the qtpy/dbus stand-ins)
from qtpy.QtWidgets import QHBoxLayout, QLabel
from ui.components import HelpIcon
from ui.helpers import (
    HELPERS,
    add_help_icon,
    as_bool,
    bool_to_str,
    config_bool,
    local_dict_path,
    notify_parent_changed,
    set_add_button_mode,
)


class ConfigBoolTests(unittest.TestCase):
    def test_known_string_values(self):
        cases = [
            ("True", "True", True),
            ("true", "True", True),
            ("TRUE", "True", True),
            ("False", "True", False),
            ("false", "True", False),
            ("1", "True", False),
            ("yes", "True", False),
        ]
        for stored, default, expected in cases:
            with self.subTest(stored=stored):
                self.assertEqual(config_bool({"Key": stored}, "Key", default), expected)

    def test_real_bool_values_pass_through(self):
        # Values after a dbus round trip are real booleans, not "True"/"False" strings.
        self.assertTrue(config_bool({"Key": True}, "Key", "False"))
        self.assertFalse(config_bool({"Key": False}, "Key", "True"))

    def test_missing_key_falls_back_to_default(self):
        self.assertTrue(config_bool({}, "Key", "True"))
        self.assertFalse(config_bool({}, "Key", "False"))

    def test_default_default_is_true(self):
        self.assertTrue(config_bool({}, "Key"))


class AsBoolAndBoolToStrTests(unittest.TestCase):
    def test_as_bool(self):
        cases = [
            (True, True),
            ("True", True),
            ("true", True),
            ("TRUE", True),
            (False, False),
            ("False", False),
            (1, False),
            (None, False),
        ]
        for value, expected in cases:
            with self.subTest(value=value):
                self.assertEqual(as_bool(value), expected)

    def test_bool_to_str(self):
        self.assertEqual(bool_to_str(True), "True")
        self.assertEqual(bool_to_str(False), "False")

    def test_round_trip(self):
        for flag in (True, False):
            with self.subTest(flag=flag):
                self.assertEqual(as_bool(bool_to_str(flag)), flag)


class LocalDictPathTests(unittest.TestCase):
    def setUp(self):
        self._saved = {name: os.environ.get(name) for name in ("XDG_DATA_HOME", "HOME")}

    def tearDown(self):
        for name, value in self._saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value

    def test_uses_xdg_data_home_when_set(self):
        os.environ["XDG_DATA_HOME"] = "/tmp/lotus-data"
        expected = os.path.join("/tmp/lotus-data", "fcitx5/lotus/vietnamese.cm.dict")
        self.assertEqual(local_dict_path(), expected)

    def test_falls_back_to_home(self):
        os.environ.pop("XDG_DATA_HOME", None)
        os.environ["HOME"] = "/home/lotus-user"
        expected = os.path.join(
            "/home/lotus-user", ".local/share", "fcitx5/lotus/vietnamese.cm.dict"
        )
        self.assertEqual(local_dict_path(), expected)


class HelpersCatalogTests(unittest.TestCase):
    def test_documented_setting_keys_are_covered(self):
        for key in (
            "FreeMarking",
            "CapitalizeMacro",
            "AutoNonVnRestore",
            "EnableMacroInOffMode",
            "MacroSkipTriggerModifier",
        ):
            with self.subTest(key=key):
                self.assertIn(key, HELPERS)
                self.assertIsInstance(HELPERS[key], str)
                self.assertTrue(HELPERS[key])


class _ParentWindow:
    def __init__(self):
        self.calls = 0

    def window(self):
        return self

    def on_changed(self):
        self.calls += 1


class _OrphanWidget:
    def window(self):
        return object()


class NotifyParentChangedTests(unittest.TestCase):
    def test_calls_on_changed_when_available(self):
        parent = _ParentWindow()
        notify_parent_changed(parent)
        self.assertEqual(parent.calls, 1)

    def test_tolerates_windows_without_on_changed(self):
        notify_parent_changed(_OrphanWidget())


class _FakeButton:
    def __init__(self):
        self.icons = []
        self.texts = []
        self.tips = []

    def setIcon(self, icon):
        self.icons.append(icon)

    def setText(self, text):
        self.texts.append(text)

    def setToolTip(self, tip):
        self.tips.append(tip)


class SetAddButtonModeTests(unittest.TestCase):
    def test_update_mode_uses_save_icon(self):
        with mock.patch("ui.helpers.QIcon") as fake_icon:
            button = _FakeButton()
            set_add_button_mode(button, True, "Update", "Add", update_tip="save")
        fake_icon.fromTheme.assert_called_once_with("document-save")
        self.assertEqual(len(button.icons), 1)

    def test_add_mode_uses_add_icon(self):
        with mock.patch("ui.helpers.QIcon") as fake_icon:
            button = _FakeButton()
            set_add_button_mode(button, False, "Update", "Add", add_tip="add")
        fake_icon.fromTheme.assert_called_once_with("list-add")
        self.assertEqual(len(button.icons), 1)

    def test_update_mode(self):
        button = _FakeButton()
        set_add_button_mode(button, True, "Update", "Add", update_tip="save")
        self.assertEqual(button.texts, ["Update"])
        self.assertEqual(button.tips, ["save"])

    def test_add_mode(self):
        button = _FakeButton()
        set_add_button_mode(button, False, "Update", "Add", add_tip="add")
        self.assertEqual(button.texts, ["Add"])
        self.assertEqual(button.tips, ["add"])

    def test_missing_tips_are_left_untouched(self):
        button = _FakeButton()
        set_add_button_mode(button, True, "Update", "Add")
        self.assertEqual(button.tips, [])


class AddHelpIconTests(unittest.TestCase):
    def test_known_key_adds_icon(self):
        layout = QHBoxLayout()
        icon = add_help_icon(layout, "FreeMarking")
        self.assertIsInstance(icon, HelpIcon)
        self.assertEqual(layout.count(), 1)

    def test_unknown_key_adds_nothing(self):
        layout = QHBoxLayout()
        self.assertIsNone(add_help_icon(layout, "UnknownKey"))
        self.assertEqual(layout.count(), 0)

    def test_clear_existing_removes_only_help_icons(self):
        layout = QHBoxLayout()
        stale_icon = HelpIcon("stale")
        plain_label = QLabel()
        layout.addWidget(stale_icon)
        layout.addWidget(plain_label)

        fresh = add_help_icon(layout, "CapitalizeMacro", clear_existing=True)

        self.assertIsInstance(fresh, HelpIcon)
        self.assertEqual(layout.count(), 2)
        self.assertIs(layout.itemAt(0).widget(), plain_label)
        stale_icon.deleteLater.assert_called_once()


if __name__ == "__main__":
    unittest.main()
