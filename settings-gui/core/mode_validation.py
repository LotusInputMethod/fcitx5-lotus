# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shortcut validation for the dynamic settings page, testable without Qt."""

from collections.abc import Mapping


def find_duplicate_shortcuts(enabled_values: Mapping[str, str]) -> dict[str, list[str]]:
    """Groups the enabled shortcut keys by their value.

    Returns only the values carried by more than one enabled mode, mapped to
    the keys that share them; empty values are ignored.
    """
    grouped: dict[str, list[str]] = {}
    for key, value in enabled_values.items():
        if value:
            grouped.setdefault(value, []).append(key)
    return {value: keys for value, keys in grouped.items() if len(keys) > 1}
