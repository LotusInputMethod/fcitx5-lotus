# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validation and parsing rules of the editor pages, testable without Qt."""

from collections.abc import Iterable


def is_invalid_dict_word(word: str) -> bool:
    """Dictionary words must not contain spaces."""
    return bool(word) and " " in word


def is_invalid_macro_key(key: str) -> bool:
    """Macro keys allow alphanumeric characters only (including Unicode letters)."""
    return bool(key) and not key.isalnum()


def macro_add_enabled(key: str, value: str, is_invalid: bool) -> bool:
    """The Add button needs a valid, non-empty key and value."""
    return not is_invalid and bool(key) and bool(value)


def macro_defaults_changed(
    row_count: int,
    enable: bool,
    capitalize: bool,
    off_mode: bool,
    skip_value: str,
) -> bool:
    """Returns True when macro state differs from factory defaults.

    Defaults: empty table, macro and capitalization enabled, off-mode macro
    disabled, skip modifier set to "Disabled".
    """
    return row_count > 0 or not enable or not capitalize or off_mode or skip_value != "Disabled"


def parse_word_list(lines: Iterable[str]) -> list[str]:
    """Reads the dictionary word list: strips lines, skips blanks and # comments."""
    words = []
    for line in lines:
        word = line.strip()
        if word and not word.startswith("#"):
            words.append(word)
    return words
