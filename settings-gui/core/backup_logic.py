# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure backup and restore logic of the backup page, testable without Qt.

The page owns dialogs, checkboxes and message boxes; this module owns the
component registry and the collect/restore semantics over a DBus handler.
"""

import os
from datetime import datetime

# Ordered component registry: key, display label (gettext source string) and,
# for the DBus backed components, the sub config group and group name.
COMPONENT_KEYS = ("config", "macros", "keymaps", "rules", "dictionary")
COMPONENT_LABELS = {
    "config": "Main Settings",
    "macros": "Macros",
    "keymaps": "Custom Keymaps",
    "rules": "Application Rules",
    "dictionary": "Custom Dictionary",
}
DBUS_COMPONENTS = (
    ("macros", "lotus-macro", "Macro"),
    ("keymaps", "custom_keymap", "CustomKeymap"),
    ("rules", "app_rules", "Rules"),
)


def backup_file_name(now: datetime) -> str:
    """Builds the default backup file name for the given time."""
    return f"lotus-backup-{now.strftime('%Y%m%d-%H%M%S')}.json"


def build_backup_meta(selected_keys: list[str], now: datetime) -> dict:
    """Builds the meta block of a backup in the canonical component order."""
    return {
        "version": 1,
        "timestamp": now.isoformat(),
        "components": [key for key in COMPONENT_KEYS if key in selected_keys],
    }


def present_component_keys(backup: dict) -> list[str]:
    """Lists the recognized component keys of a backup, in canonical order."""
    return [key for key in COMPONENT_KEYS if key in backup]


def collect_backup(dbus, selected_keys: list[str], dict_path: str) -> dict:
    """Collects the selected components into a backup payload.

    Raises when a DBus call or the dictionary file read fails; the page turns
    that into its error message.
    """
    backup = {}
    if "config" in selected_keys:
        backup["config"] = dbus.get_config().get("values", {})
    for key, group, name in DBUS_COMPONENTS:
        if key in selected_keys:
            backup[key] = dbus.get_sub_config_list(group, name)
    if "dictionary" in selected_keys and os.path.exists(dict_path):
        with open(dict_path, "r", encoding="utf-8") as f:
            backup["dictionary"] = f.read()
    return backup


def restore_backup(dbus, backup: dict, selected_keys: list[str], dict_path: str) -> list[str]:
    """Applies the selected components of a backup.

    Returns the keys of the components that failed to restore, in the order
    the page reports them. The dictionary write swallows its error like the
    page did before; the DBus failures are reported by the handler returning
    False.
    """
    failed = []
    if "config" in selected_keys and "config" in backup:
        if not dbus.set_config(backup["config"]):
            failed.append("config")
    for key, group, name in DBUS_COMPONENTS:
        if key in selected_keys and key in backup:
            if not dbus.set_sub_config_list(group, name, backup[key]):
                failed.append(key)
    if "dictionary" in selected_keys and "dictionary" in backup:
        try:
            os.makedirs(os.path.dirname(dict_path), exist_ok=True)
            with open(dict_path, "w", encoding="utf-8") as f:
                f.write(backup["dictionary"])
        except Exception as e:
            print(f"Failed to restore dictionary file: {e}")
            failed.append("dictionary")
    return failed
