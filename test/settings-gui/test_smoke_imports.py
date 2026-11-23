# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke tests: every settings-gui module must import headlessly."""

import importlib
import sys
import unittest

import _stubs  # noqa: F401  (installs the qtpy/gi/dbus stand-ins)

EXPECTED_MODULES = (
    "i18n",
    "main",
    "core.dbus_handler",
    "adw_ui",
    "adw_ui.app_detector",
    "adw_ui.utils",
    "adw_ui.window",
    "adw_ui.widgets",
    "adw_ui.widgets.reset_dialog",
    "adw_ui.widgets.section_header",
    "adw_ui.widgets.shortcut_dialog",
    "adw_ui.pages",
    "adw_ui.pages.base",
    "adw_ui.pages.dictionary",
    "adw_ui.pages.extra",
    "adw_ui.pages.keymap",
    "adw_ui.pages.macros",
    "adw_ui.pages.mode_manager",
    "adw_ui.pages.shortcuts",
    "ui.components",
    "ui.helpers",
    "ui.main_window",
    "ui.pages.about",
    "ui.pages.backup",
    "ui.pages.base_editor",
    "ui.pages.dict_editor",
    "ui.pages.dynamic_settings",
    "ui.pages.keymap_editor",
    "ui.pages.macro_editor",
    "ui.pages.mode_manager",
)


class SmokeImportTests(unittest.TestCase):
    def test_all_settings_gui_modules_import_without_qt_or_dbus(self):
        for module_name in EXPECTED_MODULES:
            with self.subTest(module=module_name):
                module = importlib.import_module(module_name)
                self.assertIn(module_name, sys.modules)
                self.assertTrue(module.__doc__ is None or isinstance(module.__doc__, str))

    def test_main_entry_point_has_guarded_execution(self):
        module = importlib.import_module("main")
        self.assertTrue(callable(module.main))
        self.assertTrue(hasattr(module, "LotusAdwApplication"))

    def test_settings_window_class_is_exposed(self):
        adw_module = importlib.import_module("adw_ui.window")
        self.assertTrue(hasattr(adw_module, "LotusPreferencesWindow"))
        self.assertTrue(hasattr(adw_module, "LotusSettingsWindow"))
        self.assertIs(adw_module.LotusSettingsWindow, adw_module.LotusPreferencesWindow)

        qt_module = importlib.import_module("ui.main_window")
        self.assertTrue(hasattr(qt_module, "LotusSettingsWindow"))

    def test_dbus_handler_class_is_exposed(self):
        module = importlib.import_module("core.dbus_handler")
        self.assertTrue(hasattr(module, "LotusDBusHandler"))


if __name__ == "__main__":
    unittest.main()
