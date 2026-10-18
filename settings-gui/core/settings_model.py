# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Config metadata model of the dynamic settings page, testable without Qt.

Metadata rows are ``(key, type, label, default, annotations)`` tuples grouped
in ``[(group_name, [rows...]), ...]`` lists, exactly as the D-Bus config
interface delivers them.
"""

from collections.abc import Iterable, Mapping


def flatten_metadata(metadata_list: Iterable) -> dict:
    """Maps every metadata row by its config key; later groups win."""
    flat = {}
    for group in metadata_list:
        for item in group[1]:
            flat[item[0]] = item
    return flat


def defaults_from_metadata(metadata_list: Iterable) -> dict:
    """Extracts the factory default value of every metadata row."""
    defaults = {}
    for group in metadata_list:
        for item in group[1]:
            defaults[item[0]] = item[3]
    return defaults


def values_match_defaults(values: Mapping, metadata: Mapping) -> bool:
    """Returns True when every value equals its metadata default.

    Both sides are compared via ``str()`` like the config bridge delivers
    them; hotkey dicts are compared through their ``"0"`` entry only.
    """
    for key, val in values.items():
        meta = metadata.get(key)
        if not meta:
            continue
        default_val = meta[3]
        # Handle cases where default_val might be a dict (like hotkeys)
        if isinstance(default_val, dict) and isinstance(val, dict):
            if str(val.get("0")) != str(default_val.get("0")):
                return False
        elif str(val) != str(default_val):
            return False
    return True


def sorted_enum_keys(enum_dict: Mapping) -> list:
    """Sorts enum annotation keys numerically when possible, else lexically."""
    return sorted(enum_dict.keys(), key=lambda x: int(x) if str(x).isdigit() else x)
