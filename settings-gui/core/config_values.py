# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Configuration value coercion helpers, testable without Qt."""


def config_bool(values: dict, key: str, default: str = "True") -> bool:
    """Reads a boolean flag stored as a string in the configuration."""
    return str(values.get(key, default)).lower() == "true"


def as_bool(value) -> bool:
    """Converts a configuration value to a boolean."""
    return str(value).lower() == "true"


def bool_to_str(flag: bool) -> str:
    """Encodes a boolean flag the way the configuration expects it."""
    return "True" if flag else "False"


def coerce_int(value, default: int) -> int:
    """Converts a config value to int, falling back on junk or missing data."""
    try:
        return int(value)
    except (ValueError, TypeError):
        return default


def pick_supported_value(value, supported, default):
    """Returns the value when the widget supports it, else the default.

    Mirrors the combo box loading fallback: unsupported stored values fall
    back to the factory default instead of leaving the widget unset.
    """
    return value if value in supported else default
