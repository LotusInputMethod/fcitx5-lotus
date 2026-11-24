# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the GTK 4 / Libadwaita settings GUI (adw_ui)."""

import importlib
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

import _stubs  # noqa: F401  (installs the gi/dbus stand-ins)
from adw_ui.app_detector import DesktopAppIndex, clean_proc_name
from adw_ui.pages.base import StagedPage
from adw_ui.pages.dictionary import _DEFAULT_ENABLE_DICT, DictEditorPage
from adw_ui.pages.extra import (
    _BLANK_ISSUE_URL,
    _BUG_URL,
    _COMPONENTS,
    _FEATURE_URL,
    AboutPage,
    BackupPage,
    export_debug_info,
    show_about_dialog,
)
from adw_ui.pages.keymap import BAMBOO_ACTIONS, PRESETS, KeymapEditorPage
from adw_ui.pages.macros import (
    _DEFAULT_BEHAVIOR,
    DATE_PRESETS,
    SKIP_CHOICES,
    TIME_PRESETS,
    MacroEditorPage,
)
from adw_ui.pages.mode_manager import (
    DEFAULT_DELAY_KEYS,
    MODE_DEFAULT,
    MODE_EMOJI,
    MODE_INFO,
    MODE_OFF,
    MODE_PREEDIT,
    MODE_SMOOTH,
    MODE_SUPER_SMOOTH,
    MODE_SURROUNDING,
    SELECTABLE_MODES,
    ModeManagerPage,
)
from adw_ui.pages.shortcuts import (
    _MAIN_SHORTCUT_DEFAULTS,
    DEFAULT_MODE_ORDER,
    MODE_DEFS,
    ShortcutsPage,
)
from adw_ui.utils import (
    as_bool,
    bool_to_str,
    build_file_filters,
    clear_children,
    get_explicit_method,
    local_dict_path,
    make_delete_button,
    make_empty_row,
    make_icon_button,
    read_dict_words,
    save_dbus_values,
    set_entry_error,
    write_dict_words_atomic,
)
from adw_ui.widgets.reset_dialog import show_section_reset_dialog
from adw_ui.widgets.section_header import (
    attach_header_suffix_reset,
    build_list_section_header,
    make_section_reset_button,
)
from adw_ui.widgets.shortcut_dialog import (
    HOTKEY_SYM_MAP,
    HOTKEY_UI_UNSHIFT_MAP,
    ShortcutCaptureDialog,
    create_keycap_box,
    keyval_to_shortcut_string,
    pretty_format_hotkey_parts,
)
from adw_ui.window import FALLBACK_DEFAULTS, LotusPreferencesWindow, LotusSettingsWindow
from gi.repository import Adw, Gdk, Gtk

_UNSET = object()


class FakeDBusHandler:
    """In-memory LotusDBusHandler replacement for headless page tests."""

    def __init__(
        self,
        values=None,
        metadata=None,
        subconfigs=None,
        fail_save=False,
        iface=_UNSET,
    ):
        self.values = dict(values or {})
        self.metadata = list(metadata or [])
        self.subconfigs = {k: list(v) for k, v in (subconfigs or {}).items()}
        self.fail_save = fail_save
        self.iface = object() if iface is _UNSET else iface
        self.saved_configs = []
        self.saved_subconfigs = []

    def get_config(self):
        return {
            "values": dict(self.values),
            "metadata": list(self.metadata),
        }

    def set_config(self, values_dict):
        if self.fail_save:
            return False
        self.values = dict(values_dict)
        self.saved_configs.append(dict(values_dict))
        return True

    def save_values(self, updates):
        if self.fail_save:
            return False
        self.values.update(updates)
        self.saved_configs.append(dict(self.values))
        return True

    def get_sub_config_list(self, path, root_key):
        return list(self.subconfigs.get((path, root_key), []))

    def set_sub_config_list(self, path, root_key, data_list):
        if self.fail_save:
            return False
        self.subconfigs[(path, root_key)] = list(data_list)
        self.saved_subconfigs.append((path, root_key, list(data_list)))
        return True


class AdwModuleImportTests(unittest.TestCase):
    ADW_MODULES = (
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
        "adw_ui.pages.shortcuts",
        "adw_ui.pages.mode_manager",
        "adw_ui.pages.macros",
        "adw_ui.pages.dictionary",
        "adw_ui.pages.keymap",
        "adw_ui.pages.extra",
    )

    def test_all_adw_modules_import_cleanly(self):
        for module_name in self.ADW_MODULES:
            with self.subTest(module=module_name):
                module = importlib.import_module(module_name)
                self.assertIn(module_name, sys.modules)
                self.assertTrue(module.__doc__ is None or isinstance(module.__doc__, str))

    def test_window_alias_is_consistent(self):
        self.assertIs(LotusSettingsWindow, LotusPreferencesWindow)


class ShortcutDialogHelperTests(unittest.TestCase):
    def test_pretty_format_hotkey_parts_cases(self):
        cases = [
            (None, []),
            ("", []),
            ("+", ["+"]),
            ("Control++", ["Ctrl", "+"]),
            ("a", ["A"]),
            ("A", ["Shift", "A"]),
            ("Control+a", ["Ctrl", "A"]),
            ("Control+A", ["Ctrl", "A"]),
            ("f5", ["F5"]),
            ("grave", ["`"]),
            ("space", ["Space"]),
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
            ("Shift+Super+Alt+Control+x", ["Ctrl", "Alt", "Super", "Shift", "X"]),
        ]
        for hotkey, expected in cases:
            with self.subTest(hotkey=hotkey):
                self.assertEqual(pretty_format_hotkey_parts(hotkey), expected)

    def test_keyval_to_shortcut_string(self):
        self.assertEqual(keyval_to_shortcut_string(Gdk.KEY_a, 0), "a")
        self.assertEqual(keyval_to_shortcut_string(Gdk.KEY_grave, 0), "grave")
        self.assertEqual(keyval_to_shortcut_string(Gdk.KEY_ISO_Left_Tab, 0), "Tab")
        self.assertEqual(
            keyval_to_shortcut_string(Gdk.KEY_a, Gdk.ModifierType.CONTROL_MASK),
            "Control+A",
        )
        self.assertEqual(
            keyval_to_shortcut_string(
                Gdk.KEY_a,
                Gdk.ModifierType.CONTROL_MASK
                | Gdk.ModifierType.ALT_MASK
                | Gdk.ModifierType.SHIFT_MASK,
            ),
            "Control+Alt+Shift+A",
        )
        self.assertEqual(
            keyval_to_shortcut_string(Gdk.KEY_exclam, Gdk.ModifierType.SHIFT_MASK),
            "exclam",
        )
        self.assertEqual(
            keyval_to_shortcut_string(Gdk.KEY_F5, Gdk.ModifierType.SHIFT_MASK),
            "Shift+F5",
        )
        self.assertEqual(
            keyval_to_shortcut_string(
                Gdk.KEY_q,
                Gdk.ModifierType.CONTROL_MASK,
                single_key_only=True,
            ),
            "q",
        )
        self.assertEqual(keyval_to_shortcut_string(0xDEADBEEF, 0), "")

    def test_unshift_and_symbol_maps_valid(self):
        for shifted, base in HOTKEY_UI_UNSHIFT_MAP.items():
            with self.subTest(shifted=shifted, base=base):
                self.assertTrue(len(base) == 1 or base in HOTKEY_SYM_MAP)

    def test_create_keycap_box_and_capture_dialog(self):
        empty = create_keycap_box("", empty_label="None")
        self.assertEqual(empty.get_label(), "None")

        box = create_keycap_box("Control+grave")
        caps = []
        child = box.get_first_child()
        while child is not None:
            caps.append(child.get_label())
            child = child.get_next_sibling()
        self.assertEqual(caps, ["Ctrl", "`"])

        captured = []
        dlg = ShortcutCaptureDialog(
            title="Test",
            current_shortcut="grave",
            allow_single_key=False,
            single_key_only=False,
            on_captured=captured.append,
        )
        dlg.present(None)
        # Single non-special key is rejected when allow_single_key=False
        self.assertFalse(dlg._on_key_pressed(None, Gdk.KEY_a, 0, 0))
        self.assertEqual(captured, [])
        # Modifier-only tracks held modifier and refreshes preview
        self.assertTrue(dlg._on_key_pressed(None, Gdk.KEY_Control_L, 0, 0))
        self.assertIn("Control", dlg._held_mods)
        dlg._on_key_released(None, Gdk.KEY_Control_L, 0, 0)
        self.assertNotIn("Control", dlg._held_mods)
        # Valid combination triggers callback and closes
        self.assertTrue(dlg._on_key_pressed(None, Gdk.KEY_a, 0, Gdk.ModifierType.CONTROL_MASK))
        self.assertEqual(captured, ["Control+A"])
        self.assertFalse(dlg.get_mapped())

        # BackSpace clears
        dlg.present(None)
        self.assertTrue(dlg._on_key_pressed(None, Gdk.KEY_BackSpace, 0, 0))
        self.assertEqual(captured[-1], "")


class ShortcutsPageTests(unittest.TestCase):
    def test_mode_defs_match_cpp_engine(self):
        internal_names = [m[0] for m in MODE_DEFS]
        self.assertEqual(
            internal_names,
            ["Smooth", "SuperSmooth", "SurroundingText", "Preedit", "Emoji", "Off", "Default"],
        )
        self.assertNotIn("Select", internal_names)
        self.assertEqual(
            DEFAULT_MODE_ORDER,
            "Smooth,SurroundingText,Preedit,Emoji,Off,SuperSmooth,Default",
        )
        self.assertEqual(_MAIN_SHORTCUT_DEFAULTS["ModeMenuKey"], "grave")
        page = ShortcutsPage(FakeDBusHandler())
        self.assertEqual(page.current_main_shortcuts["ModeMenuKey"], "grave")

    def test_staged_modifications_validation_apply_and_cancel(self):
        dbus = FakeDBusHandler(
            values={
                "ModeMenuKey": {"0": "grave"},
                "CycleModeKey": {},
                "ModeOrder": DEFAULT_MODE_ORDER,
                "ShowModeSmooth": "True",
                "ShortcutSmooth": "1",
                "ShowModeSuperSmooth": "True",
                "ShortcutSuperSmooth": "a",
            }
        )
        events = []
        page = ShortcutsPage(
            dbus,
            on_modified_changed=lambda pid, mod, app, can, has_error=False, error_msg="": (
                events.append((pid, mod, has_error, error_msg))
            ),
        )
        self.assertFalse(page.is_modified())
        self.assertEqual(events[-1], ("shortcuts", False, False, ""))

        # Reordering stages ModeOrder change
        page._move_mode_down("Smooth")
        self.assertTrue(page.is_modified())
        self.assertEqual(page.enabled_modes[0], "SurroundingText")
        self.assertEqual(events[-1][0:3], ("shortcuts", True, False))

        # Duplicate shortcut triggers error and blocks apply
        page.current_shortcuts["SuperSmooth"] = "1"
        page._check_modified()
        self.assertTrue(events[-1][2])
        self.assertFalse(page.apply_changes())

        # Disabling the conflicting mode clears the error
        page._on_switch_toggled("SuperSmooth", False)
        self.assertFalse(events[-1][2])

        # Cancel discards all staged edits
        page.cancel_changes()
        self.assertFalse(page.is_modified())
        self.assertEqual(page.current_shortcuts["SuperSmooth"], "a")
        self.assertIn("SuperSmooth", page.enabled_modes)

        # Apply persists changes and resets baseline
        page._move_mode_down("Smooth")
        page.current_main_shortcuts["CycleModeKey"] = "Control+grave"
        page._check_modified()
        self.assertTrue(page.apply_changes())
        self.assertFalse(page.is_modified())
        self.assertEqual(dbus.values["CycleModeKey"], {"0": "Control+grave"})
        self.assertTrue(dbus.values["ModeOrder"].startswith("SurroundingText,Smooth,"))

        # Restore defaults stages changes back to defaults
        page.restore_defaults()
        self.assertTrue(page.is_modified())
        self.assertEqual(page.current_main_shortcuts["CycleModeKey"], "")


class ModeManagerPageTests(unittest.TestCase):
    def test_mode_constants_match_cpp_engine(self):
        mode_ids = [
            MODE_OFF,
            MODE_SMOOTH,
            MODE_SUPER_SMOOTH,
            MODE_SURROUNDING,
            MODE_PREEDIT,
            MODE_EMOJI,
            MODE_DEFAULT,
        ]
        self.assertEqual(len(mode_ids), len(set(mode_ids)))
        self.assertEqual(set(MODE_INFO), set(mode_ids))
        for mode_id in SELECTABLE_MODES:
            self.assertIn(mode_id, MODE_INFO)
            self.assertNotEqual(mode_id, MODE_DEFAULT)
        self.assertEqual(
            DEFAULT_DELAY_KEYS,
            {"DefaultCommitInterval": 20, "DefaultBackspaceInterval": 5, "DefaultPostDelay": 10},
        )

    def test_legacy_modes_normalized_and_staged_callbacks(self):
        dbus = FakeDBusHandler(
            values={
                "DefaultCommitInterval": "25",
                "DefaultBackspaceInterval": "5",
                "DefaultPostDelay": "10",
            },
            subconfigs={
                ("app_rules", "Rules"): [
                    {
                        "App": "legacy-select",
                        "Mode": "7",
                        "CommitInterval": "0",
                        "BackspaceInterval": "0",
                        "PostDelay": "0",
                    },
                    {
                        "App": "legacy-slow",
                        "Mode": "2",
                        "CommitInterval": "15",
                        "BackspaceInterval": "0",
                        "PostDelay": "0",
                    },
                ]
            },
        )
        events = []
        with mock.patch.object(ModeManagerPage, "_scan_desktop_files"):
            page = ModeManagerPage(
                dbus,
                on_modified_changed=lambda pid, mod, app, can: events.append((pid, mod)),
            )

        self.assertFalse(page.is_modified())
        self.assertEqual(page.app_rules["legacy-select"]["mode"], MODE_SMOOTH)
        self.assertEqual(page.app_rules["legacy-slow"]["mode"], MODE_SMOOTH)

        # Add rule, update delay, then cancel
        page._add_or_select_app("discord & chat")
        page._on_app_mode_changed("discord & chat", MODE_PREEDIT)
        page._on_default_delay_changed("DefaultPostDelay", 30)
        self.assertTrue(page.is_modified())
        self.assertEqual(events[-1], ("applications", True))

        page.cancel_changes()
        self.assertFalse(page.is_modified())
        self.assertNotIn("discord & chat", page.app_rules)

        # Add rule and apply
        page._add_or_select_app("kitty")
        page._on_app_mode_changed("kitty", MODE_SURROUNDING)
        self.assertTrue(page.apply_changes())
        self.assertFalse(page.is_modified())
        saved_apps = {r["App"]: r for r in dbus.subconfigs[("app_rules", "Rules")]}
        self.assertEqual(saved_apps["kitty"]["Mode"], str(MODE_SURROUNDING))

        # Restore defaults clears staged rules and resets delays
        page.restore_defaults()
        self.assertTrue(page.is_modified())
        self.assertEqual(page.app_rules, {})
        self.assertEqual(page._current_delays, DEFAULT_DELAY_KEYS)

    def test_detect_running_app_dialog_and_inline_remove(self):
        dbus = FakeDBusHandler(
            subconfigs={
                ("app_rules", "Rules"): [
                    {
                        "App": "firefox",
                        "Mode": str(MODE_PREEDIT),
                        "CommitInterval": "0",
                        "BackspaceInterval": "0",
                        "PostDelay": "0",
                    },
                ]
            }
        )
        with (
            mock.patch.object(ModeManagerPage, "_scan_desktop_files"),
            mock.patch.object(
                ModeManagerPage,
                "_detect_running_apps",
                return_value=[
                    {"app_id": "firefox", "name": "Firefox"},
                    {"app_id": "ABDownloadManager", "name": "AB Download Manager"},
                ],
            ),
        ):
            page = ModeManagerPage(dbus)
            self.assertNotIn("detect_popover", page.__dict__)

            # Collapsed expander header suffix contains only the mode pill (no delete button)
            expander_children = page._row_widgets["firefox"]["expander"]._children
            self.assertIn(page._row_widgets["firefox"]["mode_badge"], expander_children)
            self.assertNotIn(page._row_widgets["firefox"]["btn_del"], expander_children)

            # Clicking Detect opens Adw.Dialog and populates rows
            page.btn_detect.emit("clicked")
            self.assertIsNotNone(page.detect_dialog)
            self.assertTrue(page.detect_dialog.get_mapped())
            self.assertEqual(len(page.detect_listbox._children), 2)

            # Search filters running apps inside the dialog
            page.detect_search_entry.set_text("download")
            page.detect_search_entry.emit("search-changed")
            self.assertEqual(len(page.detect_listbox._children), 1)

            # Activating unadded row adds rule and closes dialog
            page.detect_listbox._children[0].emit("activated")
            self.assertIn("ABDownloadManager", page.app_rules)
            self.assertFalse(page.detect_dialog.get_mapped())

            # Remove button inside expanded row deletes the rule
            page._row_widgets["ABDownloadManager"]["btn_del"].emit("clicked")
            self.assertNotIn("ABDownloadManager", page.app_rules)

    def test_section_reset_factory_defaults_persist_and_confirm(self):
        dbus = FakeDBusHandler(
            values={
                "DefaultCommitInterval": "35",
                "DefaultBackspaceInterval": "5",
                "DefaultPostDelay": "10",
            },
            subconfigs={
                ("app_rules", "Rules"): [
                    {
                        "App": "firefox",
                        "Mode": str(MODE_PREEDIT),
                        "CommitInterval": "0",
                        "BackspaceInterval": "0",
                        "PostDelay": "0",
                    }
                ]
            },
        )
        with mock.patch.object(ModeManagerPage, "_scan_desktop_files"):
            page = ModeManagerPage(dbus)

        # Persisted non-default values show section reset buttons immediately on load
        self.assertTrue(page.btn_reset_timing.get_visible())
        self.assertTrue(page.btn_reset_rules.get_visible())
        self.assertFalse(page.is_modified())

        # Clicking timing reset opens confirmation popup with current -> default without mutating yet
        with mock.patch.object(
            page, "_confirm_section_reset", wraps=page._confirm_section_reset
        ) as spy:
            page.btn_reset_timing.emit("clicked")
            spy.assert_called_once()
            _, changes, _ = spy.call_args[0]
            self.assertEqual(changes, [("Commit Interval (ms)", "35 ms", "20 ms")])
        self.assertEqual(page._current_delays["DefaultCommitInterval"], 35)

        # Cancelling leaves values unchanged
        page._reset_dialog.emit("response", "cancel")
        self.assertEqual(page._current_delays["DefaultCommitInterval"], 35)
        self.assertTrue(page.btn_reset_timing.get_visible())

        # Confirming factory-resets timing, persists immediately, and keeps app rules intact
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_timing.get_visible())
        self.assertTrue(page.btn_reset_rules.get_visible())
        self.assertEqual(page._current_delays, DEFAULT_DELAY_KEYS)
        self.assertEqual(dbus.values["DefaultCommitInterval"], "20")
        self.assertIn("firefox", page.app_rules)

        # Stage an edit in rules, then factory-reset rules section
        page._add_or_select_app("kitty")
        page.btn_reset_rules.emit("clicked")
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_rules.get_visible())
        self.assertEqual(page.app_rules, {})
        self.assertEqual(dbus.subconfigs[("app_rules", "Rules")], [])
        self.assertFalse(page.is_modified())


class MacroEditorPageTests(unittest.TestCase):
    def test_presets_and_staged_callbacks(self):
        self.assertTrue(SKIP_CHOICES)
        self.assertTrue(TIME_PRESETS)
        self.assertTrue(DATE_PRESETS)
        self.assertEqual(_DEFAULT_BEHAVIOR["EnableMacro"], True)

        dbus = FakeDBusHandler(
            values={"EnableMacro": "True", "CapitalizeMacro": "True"},
            subconfigs={("lotus-macro", "Macro"): [{"Key": "tphcm", "Value": "TP.HCM"}]},
        )
        events = []
        page = MacroEditorPage(
            dbus,
            on_modified_changed=lambda pid, mod, app, can, has_error=False, error_msg="": (
                events.append((pid, mod, has_error, error_msg))
            ),
        )
        self.assertFalse(page.is_modified())
        self.assertFalse(page.has_validation_errors())

        # Invalid macro key blocks saving and reports error
        page.macros.append({"Key": "bad key!", "Value": "A & B"})
        page._refresh_table()
        page._notify_modified()
        self.assertTrue(page.has_validation_errors())
        self.assertTrue(events[-1][2])
        self.assertFalse(page.apply_changes())

        # Cancel restores clean baseline
        page.cancel_changes()
        self.assertFalse(page.is_modified())
        self.assertFalse(page.has_validation_errors())

        # Valid edit and apply
        page.macros.append({"Key": "vn", "Value": "Việt Nam & Friends"})
        page._refresh_table()
        page._notify_modified()
        self.assertTrue(page.is_modified())
        self.assertTrue(page.apply_changes())
        self.assertFalse(page.is_modified())
        self.assertEqual(len(dbus.subconfigs[("lotus-macro", "Macro")]), 2)

        page.restore_defaults()
        self.assertTrue(page.is_modified())
        self.assertEqual(page.macros, [])

    def test_macro_row_layout_no_crushed_key_and_line_cap(self):
        dbus = FakeDBusHandler(
            subconfigs={
                ("lotus-macro", "Macro"): [
                    {"Key": "kg", "Value": "khô gà"},
                    {
                        "Key": "branch",
                        "Value": "Tạo branch mới từ upstream dev và thực hiện các công việc sau đó rất dài",
                    },
                    {"Key": "Alpha", "Value": "A"},
                ]
            }
        )
        page = MacroEditorPage(dbus)
        self.assertFalse(page.is_modified())
        # Sorted alphabetically case-insensitively by Key (Alpha, branch, kg)
        self.assertEqual([m["Key"] for m in page.macros], ["Alpha", "branch", "kg"])
        self.assertEqual(len(page._macro_rows), 3)

        branch_row = page._macro_rows[1]
        self.assertIn("Tạo branch mới", branch_row.get_title())
        self.assertIn("branch → Tạo branch mới", branch_row.get_tooltip_text())

        # Extract prefix key label: must not be truncated for 6-char keys
        prefix_box = branch_row._children[0]
        key_col = prefix_box._children[0]
        key_badge = key_col._children[0]
        self.assertEqual(key_badge.get_label(), "branch")
        self.assertTrue(key_col.has_css_class("macro-key-col"))
        self.assertTrue(key_badge.has_css_class("macro-key-badge"))

        # Suffix contains only Delete (clicking the row itself opens Edit)
        self.assertEqual(len(branch_row._children), 2)
        suffix_btn = branch_row._children[1]
        self.assertTrue(suffix_btn.has_css_class("destructive-action"))
        self.assertEqual(suffix_btn.get_tooltip_text(), "Remove")

        # Spacious Edit Macro Adw.Dialog supports multi-line text, $TIME/$DATE chips, validation & sorting
        page._open_macro_dialog(1)
        dlg = page._macro_dialog
        self.assertTrue(dlg.get_mapped())
        self.assertEqual(dlg._entry_key.get_text(), "branch")
        dlg._entry_key.set_text("bad key!")
        self.assertTrue(dlg._err_label.get_visible())
        self.assertFalse(dlg._btn_save.get_sensitive())

        dlg._entry_key.set_text("aa")
        self.assertFalse(dlg._err_label.get_visible())
        dlg._btn_time.emit("clicked")
        self.assertTrue(dlg._btn_save.get_sensitive())
        dlg._btn_save.emit("clicked")
        self.assertFalse(dlg.get_mapped())
        self.assertEqual([m["Key"] for m in page.macros], ["aa", "Alpha", "kg"])
        self.assertTrue(page.macros[0]["Value"].endswith("$TIME"))

    def test_section_reset_factory_defaults_persist_and_confirm(self):
        dbus = FakeDBusHandler(
            values={"EnableMacro": "True", "MacroSkipTriggerModifier": "Shift"},
            subconfigs={("lotus-macro", "Macro"): [{"Key": "vn", "Value": "Việt Nam"}]},
        )
        page = MacroEditorPage(dbus)
        # Non-default persisted skip modifier and non-empty macros show reset buttons on load
        self.assertTrue(page.btn_reset_behavior.get_visible())
        self.assertTrue(page.btn_reset_table.get_visible())

        with mock.patch.object(
            page, "_confirm_section_reset", wraps=page._confirm_section_reset
        ) as spy:
            page.btn_reset_behavior.emit("clicked")
            spy.assert_called_once()
            _, changes, _ = spy.call_args[0]
            self.assertEqual(changes, [("Skip Modifier", "Shift", "Disabled")])

        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_behavior.get_visible())
        self.assertTrue(page.btn_reset_table.get_visible())
        self.assertEqual(dbus.values["MacroSkipTriggerModifier"], "Disabled")
        self.assertEqual(len(page.macros), 1)

        page.btn_reset_table.emit("clicked")
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_table.get_visible())
        self.assertEqual(page.macros, [])
        self.assertEqual(dbus.subconfigs[("lotus-macro", "Macro")], [])
        self.assertFalse(page.is_modified())


class DictEditorPageTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self._orig_xdg = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = self._tmp.name

    def tearDown(self):
        if self._orig_xdg is None:
            os.environ.pop("XDG_DATA_HOME", None)
        else:
            os.environ["XDG_DATA_HOME"] = self._orig_xdg
        self._tmp.cleanup()

    def test_staged_edit_validation_and_apply(self):
        local_dir = os.path.join(self._tmp.name, "fcitx5/lotus")
        os.makedirs(local_dir, exist_ok=True)
        local_dict = os.path.join(local_dir, "vietnamese.cm.dict")
        with open(local_dict, "w", encoding="utf-8") as f:
            f.write("lotus\n")

        dbus = FakeDBusHandler(values={"EnableDictionary": "False"})
        events = []
        page = DictEditorPage(
            dbus,
            on_modified_changed=lambda pid, mod, app, can, has_error=False, error_msg="": (
                events.append((pid, mod, has_error, error_msg))
            ),
        )
        self.assertFalse(page.is_modified())
        self.assertEqual(page.words, ["lotus"])

        # Space in word triggers validation error and prevents apply
        page.words.append("bad word")
        page._notify_modified()
        self.assertTrue(page.has_validation_errors())
        self.assertTrue(events[-1][2])
        self.assertFalse(page.apply_changes())

        page.cancel_changes()
        self.assertFalse(page.is_modified())
        self.assertFalse(page.has_validation_errors())

        # Enable dictionary and add valid word (with '&' to check markup safety)
        page.row_enable.set_active(True)
        page.words.insert(0, "r&d")
        page._refresh_words()
        page._notify_modified()
        self.assertTrue(page.is_modified())
        self.assertTrue(page.apply_changes())
        self.assertFalse(page.is_modified())
        self.assertEqual(dbus.values["EnableDictionary"], "True")
        with open(local_dict, "r", encoding="utf-8") as f:
            self.assertEqual(f.read().splitlines(), ["r&d", "lotus"])

        # Factory reset buttons appear for non-default toggle / local dictionary override
        self.assertTrue(page.btn_reset_config.get_visible())
        self.assertTrue(page.btn_reset_words.get_visible())

        page.btn_reset_config.emit("clicked")
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_config.get_visible())
        self.assertFalse(page.row_enable.get_active())
        self.assertEqual(dbus.values["EnableDictionary"], "False")

        page.btn_reset_words.emit("clicked")
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_words.get_visible())
        self.assertFalse(os.path.exists(local_dict))
        self.assertFalse(page.is_modified())

    def test_enable_dictionary_factory_default_is_false(self):
        self.assertFalse(_DEFAULT_ENABLE_DICT)
        dbus = FakeDBusHandler()
        page = DictEditorPage(dbus)
        self.assertFalse(page.row_enable.get_active())
        self.assertFalse(page.is_modified_from_default())
        self.assertFalse(page.btn_reset_config.get_visible())

        page.row_enable.set_active(True)
        self.assertTrue(page.is_modified_from_default())
        self.assertTrue(page.btn_reset_config.get_visible())
        self.assertEqual(page.get_factory_reset_changes(), [("Enable Dictionary", "On", "Off")])

        page.restore_defaults()
        self.assertFalse(page.row_enable.get_active())
        self.assertFalse(page.is_modified_from_default())

    def test_dictionary_uses_multicolumn_chip_flowbox(self):
        words = ["a", "ai", "am", "an", "ang", "anh", "ao", "au", "ay", "ba", "ban", "bang"]
        local_dir = os.path.join(self._tmp.name, "fcitx5/lotus")
        os.makedirs(local_dir, exist_ok=True)
        local_dict = os.path.join(local_dir, "vietnamese.cm.dict")
        with open(local_dict, "w", encoding="utf-8") as f:
            f.write("\n".join(words) + "\n")

        dbus = FakeDBusHandler(values={"EnableDictionary": "True"})
        page = DictEditorPage(dbus)
        self.assertIsNotNone(page.flowbox)
        self.assertEqual(len(page._word_rows), len(words))
        self.assertEqual(len(page.flowbox._children), len(words))
        first_chip = page._word_rows[0]
        self.assertTrue(first_chip.has_css_class("dict-word-chip"))
        # Clicking chip delete button removes the word from staged list
        del_btn = first_chip._children[1]
        del_btn.emit("clicked")
        self.assertNotIn("a", page.words)
        self.assertEqual(len(page._word_rows), len(words) - 1)


class KeymapEditorPageTests(unittest.TestCase):
    def test_presets_cover_valid_actions_and_callbacks(self):
        action_codes = {code for code, _ in BAMBOO_ACTIONS}
        for preset_name, bindings in PRESETS.items():
            with self.subTest(preset=preset_name):
                self.assertTrue(bindings)
                for _key, action in bindings:
                    self.assertIn(action, action_codes)

        dbus = FakeDBusHandler(values={"EnableCustomKeymap": "False"})
        events = []
        page = KeymapEditorPage(
            dbus,
            on_modified_changed=lambda pid, mod, app, can: events.append((pid, mod)),
        )
        self.assertFalse(page.is_modified())

        page.row_enable.set_active(True)
        page._apply_preset("VNI Bàn phím tiếng Pháp")
        self.assertTrue(page.is_modified())
        self.assertEqual(events[-1], ("keymap", True))

        page.cancel_changes()
        self.assertFalse(page.is_modified())
        self.assertEqual(page.keymaps, [])

        page.row_enable.set_active(True)
        page._apply_preset("Telex")
        self.assertTrue(page.apply_changes())
        self.assertFalse(page.is_modified())
        self.assertEqual(dbus.values["EnableCustomKeymap"], "True")
        self.assertEqual(
            len(dbus.subconfigs[("custom_keymap", "CustomKeymap")]), len(PRESETS["Telex"])
        )

        page.restore_defaults()
        self.assertTrue(page.is_modified())
        self.assertEqual(page.keymaps, [])

    def test_section_reset_factory_defaults_persist_and_confirm(self):
        dbus = FakeDBusHandler(
            values={"EnableCustomKeymap": "True"},
            subconfigs={("custom_keymap", "CustomKeymap"): [{"Key": "s", "Value": "DauSac"}]},
        )
        page = KeymapEditorPage(dbus)
        # Non-default persisted toggle and non-empty bindings show reset buttons on load
        self.assertTrue(page.btn_reset_config.get_visible())
        self.assertTrue(page.btn_reset_bindings.get_visible())

        page.btn_reset_config.emit("clicked")
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_config.get_visible())
        self.assertTrue(page.btn_reset_bindings.get_visible())
        self.assertEqual(dbus.values["EnableCustomKeymap"], "False")

        page.btn_reset_bindings.emit("clicked")
        page._reset_dialog.emit("response", "reset")
        self.assertFalse(page.btn_reset_bindings.get_visible())
        self.assertEqual(page.keymaps, [])
        self.assertEqual(dbus.subconfigs[("custom_keymap", "CustomKeymap")], [])
        self.assertFalse(page.is_modified())


class BackupPageTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self._orig_xdg = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = self._tmp.name

    def tearDown(self):
        if self._orig_xdg is None:
            os.environ.pop("XDG_DATA_HOME", None)
        else:
            os.environ["XDG_DATA_HOME"] = self._orig_xdg
        self._tmp.cleanup()

    def test_selective_export_and_import(self):
        self.assertEqual(
            [c[0] for c in _COMPONENTS], ["config", "rules", "macros", "keymaps", "dictionary"]
        )

        dbus = FakeDBusHandler(
            values={"Mode": "Preedit", "InputMethod": "Telex"},
            subconfigs={
                ("app_rules", "Rules"): [{"App": "kitty", "Mode": "4"}],
                ("lotus-macro", "Macro"): [{"Key": "hi", "Value": "xin chào"}],
                ("custom_keymap", "CustomKeymap"): [{"Key": "s", "Value": "DauSac"}],
            },
        )
        reloaded = []
        page = BackupPage(dbus, on_reload_requested=lambda: reloaded.append(True))

        # Empty selection is rejected
        for sw in page.switches.values():
            sw.set_active(False)
        backup_path = os.path.join(self._tmp.name, "backup.json")
        self.assertFalse(page._do_export(backup_path))

        # Export selected components (no local dict override -> dictionary omitted)
        for sw in page.switches.values():
            sw.set_active(True)
        self.assertTrue(page._do_export(backup_path))
        with open(backup_path, "r", encoding="utf-8") as f:
            exported = json.load(f)
        self.assertEqual(exported["config"]["Mode"], "Preedit")
        self.assertEqual(exported["rules"], [{"App": "kitty", "Mode": "4"}])
        self.assertNotIn("dictionary", exported)
        self.assertNotIn("dictionary", exported["meta"]["components"])

        # Mutate dbus and import backup
        dbus.values["Mode"] = "OFF"
        self.assertTrue(page._do_import(backup_path))
        self.assertEqual(dbus.values["Mode"], "Preedit")
        self.assertEqual(reloaded, [True])

    def test_offline_export_rejected_and_local_dict_omitted_or_ordered_before_set_config(self):
        # Offline Fcitx5 rejects D-Bus exports and does not write a file
        offline_dbus = FakeDBusHandler(iface=None)
        offline_page = BackupPage(offline_dbus)
        toasts = []
        offline_page._show_toast = toasts.append
        offline_path = os.path.join(self._tmp.name, "offline.json")
        self.assertFalse(offline_page._do_export(offline_path))
        self.assertFalse(os.path.exists(offline_path))
        self.assertEqual(toasts[-1], "Fcitx5 is not running. Cannot export settings.")

        # Local-dictionary-only export still succeeds offline when local override exists
        write_dict_words_atomic(local_dict_path(), ["khongdau"])
        for key, sw in offline_page.switches.items():
            sw.set_active(key == "dictionary")
        self.assertTrue(offline_page._do_export(offline_path))
        with open(offline_path, "r", encoding="utf-8") as f:
            dict_only_export = json.load(f)
        self.assertEqual(dict_only_export["dictionary"], ["khongdau"])

        # Importing config + dictionary writes local dict BEFORE calling set_config()
        os.remove(local_dict_path())
        observed_words_at_set_config = []

        class RecordingDBusHandler(FakeDBusHandler):
            def set_config(self, values_dict):
                if os.path.exists(local_dict_path()):
                    observed_words_at_set_config.append(read_dict_words(local_dict_path()))
                else:
                    observed_words_at_set_config.append(None)
                return super().set_config(values_dict)

        dbus = RecordingDBusHandler(values={"Mode": "OFF"})
        page = BackupPage(dbus)
        bundle_path = os.path.join(self._tmp.name, "bundle.json")
        with open(bundle_path, "w", encoding="utf-8") as f:
            json.dump(
                {
                    "config": {"Mode": "Preedit"},
                    "dictionary": ["lotusword", "customword"],
                },
                f,
            )

        self.assertTrue(page._do_import(bundle_path))
        self.assertEqual(observed_words_at_set_config, [["lotusword", "customword"]])
        self.assertEqual(len(dbus.saved_configs), 1)

        # Dictionary-only import (without config) triggers reload_dbus_config after write
        dict_only_path = os.path.join(self._tmp.name, "dict-only.json")
        with open(dict_only_path, "w", encoding="utf-8") as f:
            json.dump({"dictionary": ["onlydict"]}, f)
        self.assertTrue(page._do_import(dict_only_path))
        self.assertEqual(observed_words_at_set_config[-1], ["onlydict"])
        self.assertEqual(len(dbus.saved_configs), 2)

    def test_components_translate_at_setup_time(self):
        with mock.patch("adw_ui.pages.extra._", side_effect=lambda s: f"TR:{s}"):
            page = BackupPage(FakeDBusHandler())
        self.assertEqual(page.switches["config"].get_title(), "TR:General & Typing")
        self.assertEqual(
            page.switches["config"].get_tooltip_text(),
            "TR:Main input method, typing rules, shortcuts, and appearance",
        )


class AboutPageTests(unittest.TestCase):
    def test_issue_templates_about_dialog_and_debug_export(self):
        self.assertTrue(_BUG_URL.endswith("template=bug_report.yml"))
        self.assertTrue(_FEATURE_URL.endswith("template=feature_request.yml"))
        self.assertTrue(_BLANK_ISSUE_URL.endswith("/issues/new"))

        dbus = FakeDBusHandler(values={"Mode": "Preedit", "InputMethod": "Telex"})
        with tempfile.TemporaryDirectory() as tmpdir:
            debug_path = os.path.join(tmpdir, "lotus-debug.log")
            fake_proc = mock.MagicMock(returncode=0, stdout="sample output", stderr="")
            with mock.patch(
                "adw_ui.pages.extra.subprocess.run", return_value=fake_proc
            ) as run_mock:
                ok = export_debug_info(debug_path, dbus_handler=dbus)
            self.assertTrue(ok)
            self.assertTrue(os.path.exists(debug_path))
            with open(debug_path, "r", encoding="utf-8") as f:
                report = f.read()
            self.assertIn("Fcitx5 Lotus Debug Information", report)
            self.assertIn("Lotus Configuration", report)
            self.assertIn('"InputMethod": "Telex"', report)
            self.assertIn("journalctl -t fcitx5-lotus-server", report)
            self.assertIn("fcitx5-diagnose", report)
            self.assertGreaterEqual(run_mock.call_count, 3)

        page = AboutPage(dbus_handler=dbus)
        self.assertTrue(page.has_css_class("about-page"))
        issue_group = page._children[1]
        self.assertEqual(len(issue_group._children), 3)

        licenses_group = page._children[3]
        license_rows = [
            (row.get_title(), row._children[0].get_label()) for row in licenses_group._children
        ]
        self.assertEqual(
            license_rows,
            [
                ("Fcitx5 Lotus", "GPL-3.0-or-later"),
                ("Bamboo Core Engine", "MIT"),
                ("fcitx5-bamboo", "LGPL-2.1-or-later"),
            ],
        )

        parent = mock.MagicMock(dbus_handler=dbus)
        dlg = show_about_dialog(parent)
        self.assertTrue(dlg.get_mapped())


class LotusPreferencesWindowTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self._orig_xdg = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = self._tmp.name

    def tearDown(self):
        if self._orig_xdg is None:
            os.environ.pop("XDG_DATA_HOME", None)
        else:
            os.environ["XDG_DATA_HOME"] = self._orig_xdg
        self._tmp.cleanup()

    def test_window_staging_apply_cancel_and_reset(self):
        fake_dbus = FakeDBusHandler(
            values={
                "InputMethod": "VNI",
                "Mode": "Uinput (Smooth)",
                "SpellCheck": "False",
                "ModeMenuKey": {"0": "grave"},
                "CycleModeKey": {},
                "ModeOrder": DEFAULT_MODE_ORDER,
            }
        )
        with (
            mock.patch("adw_ui.window.LotusDBusHandler", return_value=fake_dbus),
            mock.patch.object(ModeManagerPage, "_scan_desktop_files"),
        ):
            win = LotusPreferencesWindow()

        self.assertEqual(len(win.subpages), 6)
        self.assertNotIn("btn_ok", win.__dict__)
        self.assertNotIn("btn_reset", win.__dict__)
        self.assertEqual(win.btn_about.get_tooltip_text(), "About")
        self.assertFalse(win.action_revealer.get_visible())
        self.assertFalse(win.btn_apply.get_sensitive())
        self.assertFalse(win.btn_cancel.get_sensitive())

        # Shortcuts mode rows place Up/Down arrows on the right, just left of the keybind button
        smooth_widgets = win.sc_page.mode_widgets["Smooth"]
        row_children = smooth_widgets["row"]._children
        self.assertEqual(
            row_children,
            [
                smooth_widgets["warning_badge"],
                smooth_widgets["box_arrows"],
                smooth_widgets["btn_key"],
                smooth_widgets["switch"],
            ],
        )

        # ToastOverlay is scoped inside the content pane (not wrapping split_view)
        self.assertIsNot(win.get_content(), win.toast_overlay)
        self.assertIs(win.get_content(), win.split_view)

        # Toggle window switch stages change and reveals floating pill bar
        spell_row, _ = win.registered_switches["SpellCheck"]
        spell_row.set_active(True)
        self.assertIn("SpellCheck", win.pending_window_values)
        self.assertTrue(win.action_revealer.get_visible())
        self.assertTrue(win.btn_apply.get_sensitive())
        self.assertTrue(win.btn_cancel.get_sensitive())

        # Cancel reverts pending window and subpage edits and hides floating pill bar
        win.sc_page._move_mode_down("Smooth")
        win._on_cancel_clicked()
        self.assertFalse(win.action_revealer.get_visible())
        self.assertFalse(win.btn_apply.get_sensitive())
        self.assertFalse(win.btn_cancel.get_sensitive())
        self.assertFalse(spell_row.get_active())
        self.assertFalse(win.sc_page.is_modified())

        # Apply saves, hides floating bar, shows save toast; next edit dismisses stale save toast
        spell_row.set_active(True)
        self.assertTrue(win._on_apply_clicked())
        self.assertFalse(win.action_revealer.get_visible())
        self.assertIsNotNone(win._save_toast)
        save_toast = win._save_toast
        spell_row.set_active(False)
        self.assertIsNone(win._save_toast)
        self.assertNotIn(save_toast, win._visible_toasts)
        self.assertTrue(win.action_revealer.get_visible())
        win._on_cancel_clicked()

        # Subpage validation error keeps floating bar visible for Cancel while blocking Apply
        win.set_page_modified("macros", True, has_error=True, error_msg="Bad macro")
        self.assertTrue(win.action_revealer.get_visible())
        self.assertFalse(win.btn_apply.get_sensitive())
        self.assertTrue(win.btn_cancel.get_sensitive())
        self.assertIn("Bad macro", win.btn_apply.get_tooltip_text())

        win.set_page_modified("macros", False, has_error=False)
        self.assertFalse(win.action_revealer.get_visible())

        # Reset defaults persists factory defaults immediately and hides floating pill bar
        win._restore_defaults()
        self.assertFalse(win.action_revealer.get_visible())
        self.assertFalse(win.btn_apply.get_sensitive())
        self.assertEqual(fake_dbus.values["InputMethod"], "Telex")
        self.assertEqual(fake_dbus.values["Mode"], "Preedit")
        self.assertEqual(fake_dbus.values["UseLotusIcons"], "False")
        self.assertEqual(fake_dbus.values["ModernStyle"], "True")
        self.assertEqual(fake_dbus.values["ModeMenuKey"], {"0": "grave"})
        self.assertEqual(fake_dbus.values["EnableDictionary"], "False")
        self.assertEqual(fake_dbus.values["SpellCheck"], "True")

    def test_fallback_defaults_match_cpp_config(self):
        self.assertEqual(FALLBACK_DEFAULTS["Mode"], "Preedit")
        self.assertEqual(FALLBACK_DEFAULTS["UseLotusIcons"], "False")
        self.assertEqual(FALLBACK_DEFAULTS["ModernStyle"], "True")

    def test_listbox_sidebar_navigation_switches_pages(self):
        fake_dbus = FakeDBusHandler()
        with (
            mock.patch("adw_ui.window.LotusDBusHandler", return_value=fake_dbus),
            mock.patch.object(ModeManagerPage, "_scan_desktop_files"),
        ):
            win = LotusPreferencesWindow()

        self.assertTrue(win.sidebar.has_css_class("navigation-sidebar"))
        self.assertEqual(len(win.sidebar._children), 8)
        selected = win.sidebar.get_selected_row()
        self.assertIsNotNone(selected)
        self.assertEqual(selected._page_id, "general")
        self.assertEqual(win.content_title.get_title(), "General")

        for row in win.sidebar._children:
            win.sidebar.select_row(row)
            vis_page = win.view_stack.get_page(win.view_stack.get_visible_child())
            self.assertEqual(vis_page.get_name(), row._page_id)
            self.assertEqual(win.content_title.get_title(), row._page_title)

    def test_offline_banner_visibility_and_retry(self):
        offline_dbus = FakeDBusHandler(iface=None)
        online_dbus = FakeDBusHandler(values={"InputMethod": "VNI"})
        with (
            mock.patch("adw_ui.window.LotusDBusHandler", side_effect=[offline_dbus, online_dbus]),
            mock.patch.object(ModeManagerPage, "_scan_desktop_files"),
        ):
            win = LotusPreferencesWindow()
            self.assertTrue(win.offline_banner.get_revealed())

            win.offline_banner.emit("button-clicked")
            self.assertIs(win.dbus_handler, online_dbus)
            for page in win.subpages:
                self.assertIs(page.dbus, online_dbus)
            self.assertFalse(win.offline_banner.get_revealed())
            method_row, option_keys, _label_map, _default = win.registered_combos["InputMethod"]
            self.assertEqual(option_keys[method_row.get_selected()], "VNI")

    def test_close_request_prompts_when_unsaved_and_respects_choice(self):
        fake_dbus = FakeDBusHandler(values={"SpellCheck": "True"})
        with (
            mock.patch("adw_ui.window.LotusDBusHandler", return_value=fake_dbus),
            mock.patch.object(ModeManagerPage, "_scan_desktop_files"),
        ):
            win = LotusPreferencesWindow()

        # Clean window closes immediately without prompting
        self.assertFalse(win._on_close_request())
        self.assertIsNone(win._close_dialog)

        # Validation error disables "apply" response
        spell_row, _ = win.registered_switches["SpellCheck"]
        spell_row.set_active(False)
        win.set_page_modified("macros", True, has_error=True, error_msg="Invalid")
        self.assertTrue(win._on_close_request())
        self.assertIsNotNone(win._close_dialog)
        self.assertTrue(win._close_dialog.get_mapped())
        self.assertFalse(win._close_dialog.get_response_enabled("apply"))

        # Cancel keeps window open
        win._close_dialog.emit("response", "cancel")
        self.assertFalse(win._closed)
        self.assertFalse(win._force_close)

        # Clearing error enables "apply"; failed save keeps window open
        win.set_page_modified("macros", False, has_error=False)
        fake_dbus.fail_save = True
        self.assertTrue(win._on_close_request())
        self.assertTrue(win._close_dialog.get_response_enabled("apply"))
        win._close_dialog.emit("response", "apply")
        self.assertFalse(win._closed)
        self.assertFalse(win._force_close)

        # Successful apply saves and closes
        fake_dbus.fail_save = False
        self.assertTrue(win._on_close_request())
        win._close_dialog.emit("response", "apply")
        self.assertTrue(win._force_close)
        self.assertTrue(win._closed)
        self.assertEqual(fake_dbus.values["SpellCheck"], "False")
        self.assertFalse(win._on_close_request())

        # Discard closes without saving
        with (
            mock.patch("adw_ui.window.LotusDBusHandler", return_value=fake_dbus),
            mock.patch.object(ModeManagerPage, "_scan_desktop_files"),
        ):
            win2 = LotusPreferencesWindow()
        win2.registered_switches["SpellCheck"][0].set_active(True)
        self.assertTrue(win2._on_close_request())
        win2._close_dialog.emit("response", "discard")
        self.assertTrue(win2._force_close)
        self.assertTrue(win2._closed)
        self.assertEqual(fake_dbus.values["SpellCheck"], "False")


class SharedUtilsTests(unittest.TestCase):
    def test_bool_conversion_and_explicit_method(self):
        self.assertTrue(as_bool(True))
        self.assertTrue(as_bool(" true "))
        self.assertFalse(as_bool("False"))
        self.assertTrue(as_bool(None, default=True))
        self.assertEqual(bool_to_str(True), "True")
        self.assertEqual(bool_to_str(False), "False")

        page = BackupPage(FakeDBusHandler())
        self.assertIsNone(get_explicit_method(page, "load_data"))
        self.assertIsNotNone(get_explicit_method(page, "_do_export"))

    def test_widget_and_entry_helpers(self):
        box = Gtk.Box()
        lbl = Gtk.Label(label="one")
        box.append(lbl)
        clear_children(box)
        self.assertEqual(box._children, [])

        empty = make_empty_row("A & B")
        self.assertFalse(empty.get_use_markup())
        self.assertEqual(empty.get_title(), "A & B")
        self.assertFalse(empty.get_activatable())

        clicked = []
        btn = make_icon_button("list-add-symbolic", "Add", on_clicked=lambda _b: clicked.append(1))
        btn.emit("clicked")
        self.assertEqual(clicked, [1])
        self.assertTrue(btn.has_css_class("flat"))

        del_btn = make_delete_button(lambda _b: clicked.append(2))
        del_btn.emit("clicked")
        self.assertEqual(clicked, [1, 2])
        self.assertTrue(del_btn.has_css_class("destructive-action"))

        entry = Gtk.Entry()
        set_entry_error(entry, "Bad value")
        self.assertTrue(entry.has_css_class("error"))
        self.assertEqual(entry.get_tooltip_text(), "Bad value")
        set_entry_error(entry, "")
        self.assertFalse(entry.has_css_class("error"))
        self.assertEqual(entry.get_tooltip_text(), "")

        filters = build_file_filters("JSON Backup", ("*.json",))
        self.assertEqual(filters.get_n_items(), 2)

    def test_dict_file_and_dbus_save_helpers(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            orig_xdg = os.environ.get("XDG_DATA_HOME")
            os.environ["XDG_DATA_HOME"] = tmpdir
            try:
                path = local_dict_path()
                self.assertTrue(path.startswith(tmpdir))
                write_dict_words_atomic(path, [" alpha ", "", "beta"])
                self.assertEqual(read_dict_words(path), ["alpha", "beta"])
            finally:
                if orig_xdg is None:
                    os.environ.pop("XDG_DATA_HOME", None)
                else:
                    os.environ["XDG_DATA_HOME"] = orig_xdg

        dbus = FakeDBusHandler(values={"A": "1"})
        self.assertTrue(save_dbus_values(dbus, {}))
        self.assertTrue(save_dbus_values(dbus, {"B": "2"}))
        self.assertEqual(dbus.values, {"A": "1", "B": "2"})

        class LegacyDBus:
            def __init__(self):
                self.values = {"A": "1"}

            def get_config(self):
                return {"values": dict(self.values)}

            def set_config(self, new_vals):
                self.values = dict(new_vals)
                return True

        legacy = LegacyDBus()
        self.assertTrue(save_dbus_values(legacy, {"C": "3"}))
        self.assertEqual(legacy.values, {"A": "1", "C": "3"})


class SectionHeaderAndDialogsTests(unittest.TestCase):
    def test_section_header_reset_and_list_controls(self):
        standalone = make_section_reset_button(lambda _b: None)
        self.assertFalse(standalone.get_visible())
        self.assertTrue(standalone.has_css_class("flat"))

        group = Adw.PreferencesGroup()
        resets = []
        btn = attach_header_suffix_reset(group, lambda: resets.append("reset"))
        self.assertFalse(btn.get_visible())
        btn.emit("clicked")
        self.assertEqual(resets, ["reset"])

        # Attaching a second reset wraps existing suffix in a horizontal Box
        btn2 = attach_header_suffix_reset(group, lambda: resets.append("reset2"))
        btn2.emit("clicked")
        self.assertEqual(resets, ["reset", "reset2"])

        list_group = Adw.PreferencesGroup()
        extra_badge = Gtk.Label(label="0")
        extra_btn = Gtk.Button()
        searches = []
        adds = []
        controls = build_list_section_header(
            list_group,
            on_reset=lambda _b: resets.append("list"),
            on_add=lambda _b: adds.append(1),
            add_tooltip="Add",
            on_search_changed=lambda e: searches.append(e.get_text()),
            prefix_widgets=[extra_badge],
            pre_add_widgets=[extra_btn],
            search_hidden=True,
        )
        self.assertFalse(controls.btn_reset.get_visible())
        self.assertFalse(controls.search_entry.get_visible())
        self.assertEqual(
            controls.box._children,
            [controls.btn_reset, extra_badge, controls.search_entry, extra_btn, controls.btn_add],
        )
        controls.btn_add.emit("clicked")
        self.assertEqual(adds, [1])

    def test_reset_dialog_confirm_and_cancel(self):
        confirmed = []
        dlg = show_section_reset_dialog(
            None,
            "Tone & Punctuation",
            [("Modern Tone", "On", "Off")],
            lambda: confirmed.append(True),
        )
        self.assertTrue(dlg.get_mapped())
        dlg.emit("response", "cancel")
        self.assertEqual(confirmed, [])
        dlg.emit("response", "reset")
        self.assertEqual(confirmed, [True])


class StagedPageAndDetectorTests(unittest.TestCase):
    def test_staged_page_guard_and_validation_forwarding(self):
        class DummyPage(StagedPage):
            PAGE_ID = "dummy"
            SUPPORTS_VALIDATION = True

            def is_modified(self):
                return True

            def has_validation_errors(self):
                return True

            def validation_message(self):
                return "Invalid"

        events = []
        page = DummyPage(
            FakeDBusHandler(),
            on_modified_changed=lambda pid, mod, app, can, has_error=False, error_msg="": (
                events.append((pid, mod, has_error, error_msg))
            ),
        )
        with page._updating_guard():
            self.assertTrue(page._updating)
            with page._updating_guard():
                self.assertTrue(page._updating)
            self.assertTrue(page._updating)
        self.assertFalse(page._updating)

        page._notify_modified()
        self.assertEqual(events, [("dummy", True, True, "Invalid")])

    def test_desktop_app_index_and_clean_proc_name(self):
        self.assertEqual(
            clean_proc_name(".firefox-wrapped", "/nix/store/x/.firefox-wrapped"), "firefox"
        )
        self.assertEqual(clean_proc_name("kitty", "/usr/bin/kitty"), "kitty")

        index = DesktopAppIndex()
        with tempfile.TemporaryDirectory() as tmpdir:
            apps_dir = os.path.join(tmpdir, "applications")
            os.makedirs(apps_dir, exist_ok=True)
            with open(
                os.path.join(apps_dir, "org.example.Editor.desktop"), "w", encoding="utf-8"
            ) as f:
                f.write("[Desktop Entry]\nName=Example Editor\nIcon=editor-icon\nExec=myedit %U\n")
            orig_xdg_dirs = os.environ.get("XDG_DATA_DIRS")
            os.environ["XDG_DATA_DIRS"] = tmpdir
            try:
                index.scan()
            finally:
                if orig_xdg_dirs is None:
                    os.environ.pop("XDG_DATA_DIRS", None)
                else:
                    os.environ["XDG_DATA_DIRS"] = orig_xdg_dirs

        self.assertEqual(index.display_name("myedit"), "Example Editor")
        self.assertEqual(index.resolve_icon_name("org.example.Editor"), "editor-icon")


if __name__ == "__main__":
    unittest.main()
