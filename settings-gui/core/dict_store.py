# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Dictionary storage logic of the dict editor page, testable without Qt."""

import os

# Fallback locations of the engine-bundled dictionary, checked in order.
GLOBAL_DICT_FALLBACKS = (
    "/usr/share/fcitx5/lotus/vietnamese.cm.dict",
    "/usr/local/share/fcitx5/lotus/vietnamese.cm.dict",
)

DICT_GRID_COLUMNS = 3


def local_dict_path() -> str:
    """Returns the path of the user dictionary edited by the settings GUI."""
    xdg_data_home = os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share"))
    return os.path.join(xdg_data_home, "fcitx5/lotus/vietnamese.cm.dict")


def global_dict_paths(primary: str) -> list[str]:
    """Builds the candidate paths of the global dictionary, primary first."""
    return [primary, *GLOBAL_DICT_FALLBACKS]


def resolve_global_dict_path(paths: list[str], exists=os.path.exists) -> str:
    """Returns the first dictionary path that exists, else the primary one."""
    for path in paths:
        if exists(path):
            return path
    return paths[0]


def pick_dict_path(local: str, fallback: str, exists=os.path.exists) -> str:
    """Prefers the user dictionary and falls back to the global one."""
    return local if exists(local) else fallback


def filter_words(words: list[str], search_text: str) -> list[str]:
    """Returns the words containing the search text, case-insensitively."""
    needle = search_text.lower()
    return [word for word in words if needle in word.lower()]


def grid_rows(count: int, cols: int = DICT_GRID_COLUMNS) -> int:
    """Returns the number of rows needed for a word grid."""
    return (count + cols - 1) // cols


def grid_position(index: int, cols: int = DICT_GRID_COLUMNS) -> tuple[int, int]:
    """Returns the (row, col) cell of a word index in the grid."""
    return index // cols, index % cols


def upsert_word(words: list[str], word: str, sort: bool = True) -> list[str]:
    """Adds a word to a copy of the list unless it is already present."""
    if word in words:
        return words
    updated = [*words, word]
    if sort:
        updated.sort()
    return updated


def remove_words(words: list[str], targets) -> list[str]:
    """Removes the first occurrence of every target word from the list."""
    updated = list(words)
    for word in targets:
        if word in updated:
            updated.remove(word)
    return updated


def dict_add_button_state(word: str, existing: list[str], is_invalid: bool) -> tuple[bool, bool]:
    """Derives the Add button enabled state and whether the word exists."""
    exists = word in existing
    enabled = not is_invalid and bool(word) and not exists
    return enabled, exists
