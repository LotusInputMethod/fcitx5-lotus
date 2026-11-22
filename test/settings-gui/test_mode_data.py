# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Consistency tests for the static data of the settings GUI pages."""

import unittest

import _stubs  # noqa: F401  (installs the qtpy/dbus stand-ins)
from ui.pages import dynamic_settings, mode_manager


class SettingsCategoryTests(unittest.TestCase):
    def test_category_values(self):
        self.assertEqual(
            {category.value for category in dynamic_settings.SettingsCategory},
            {"general", "appearance", "typing", "shortcuts", "interface"},
        )

    def test_settings_map_uses_known_categories(self):
        for category in dynamic_settings.SETTINGS_MAP:
            self.assertIsInstance(category, dynamic_settings.SettingsCategory)

    def test_settings_keys_are_unique_across_categories(self):
        seen = []
        for groups in dynamic_settings.SETTINGS_MAP.values():
            for keys in groups.values():
                seen.extend(keys)
        self.assertEqual(len(seen), len(set(seen)))
        self.assertTrue(all(seen))

    def test_settings_groups_are_non_empty(self):
        for groups in dynamic_settings.SETTINGS_MAP.values():
            for group, keys in groups.items():
                self.assertTrue(group)
                self.assertTrue(keys)

    def test_shortcut_mode_mappings_are_consistent(self):
        self.assertEqual(
            list(dynamic_settings.MODE_SHORTCUT_TO_VISIBILITY),
            dynamic_settings.MODE_SHORTCUT_KEYS,
        )
        self.assertEqual(
            set(dynamic_settings.MODE_SHORTCUT_TO_VISIBILITY),
            set(dynamic_settings.MODE_KEY_TO_INTERNAL_NAME),
        )
        for shortcut, visibility in dynamic_settings.MODE_SHORTCUT_TO_VISIBILITY.items():
            with self.subTest(shortcut=shortcut):
                self.assertTrue(visibility.startswith("ShowMode"))
                internal = dynamic_settings.MODE_KEY_TO_INTERNAL_NAME[shortcut]
                self.assertEqual(internal, visibility[len("ShowMode") :])

    def test_descriptions_cover_declared_groups(self):
        all_groups = {
            group for groups in dynamic_settings.SETTINGS_MAP.values() for group in groups
        }
        self.assertLessEqual(set(dynamic_settings.GROUP_DESCRIPTIONS), all_groups)


class ModeManagerDataTests(unittest.TestCase):
    def test_mode_ids_are_unique(self):
        mode_ids = [
            mode_manager.MODE_OFF,
            mode_manager.MODE_SMOOTH,
            mode_manager.MODE_SUPER_SMOOTH,
            mode_manager.MODE_SURROUNDING,
            mode_manager.MODE_PREEDIT,
            mode_manager.MODE_EMOJI,
            mode_manager.MODE_DEFAULT,
        ]
        self.assertEqual(len(mode_ids), len(set(mode_ids)))

    def test_selectable_modes_have_metadata(self):
        for mode_id in mode_manager.SELECTABLE_MODES:
            with self.subTest(mode_id=mode_id):
                self.assertIn(mode_id, mode_manager.MODE_INFO)
                self.assertNotEqual(mode_id, mode_manager.MODE_DEFAULT)
                self.assertTrue(mode_manager.MODE_INFO[mode_id]["title"])
                self.assertTrue(mode_manager.MODE_INFO[mode_id]["icon"])

    def test_mode_info_entries_are_complete(self):
        for mode_id, info in mode_manager.MODE_INFO.items():
            with self.subTest(mode_id=mode_id):
                self.assertIn("title", info)
                self.assertIn("icon", info)

    def test_default_delay_values_are_non_negative(self):
        for key, delay in mode_manager.DEFAULT_DELAY_KEYS.items():
            with self.subTest(key=key):
                self.assertTrue(key)
                self.assertIsInstance(delay, int)
                self.assertGreaterEqual(delay, 0)


if __name__ == "__main__":
    unittest.main()
