# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Table editor decisions shared by the editor pages, testable without Qt."""

from collections.abc import Iterable


def compute_button_states(row: int, count: int, selected: bool) -> tuple[bool, bool, bool]:
    """Derives the Up/Down/Remove enabled states for a table editor."""
    return row > 0, 0 <= row < count - 1, selected


def selected_rows(ranges: Iterable) -> list[int]:
    """Expands selection ranges into a sorted, de-duplicated row list.

    Ranges only need ``topRow`` / ``bottomRow`` attributes, so callers can
    pass either Qt selection ranges or plain test doubles.
    """
    rows = set()
    for span in ranges:
        for row in range(span.topRow(), span.bottomRow() + 1):
            rows.add(row)
    return sorted(rows)


def move_row_index(row: int, count: int, direction: int) -> int | None:
    """Returns the neighbour index of a row, or None when it cannot move."""
    if direction < 0:
        return row - 1 if row > 0 else None
    return row + 1 if 0 <= row < count - 1 else None


def collect_kv_pairs(rows: Iterable[tuple], skip_empty_key: bool = True) -> list[dict]:
    """Builds ``{"Key", "Value"}`` config entries from ``(key, value)`` rows.

    Rows whose key cell is missing (``None``) are always skipped; when
    ``skip_empty_key`` is set, rows with an empty key text are skipped too.
    """
    entries = []
    for key, value in rows:
        if key is None:
            continue
        if skip_empty_key and not key:
            continue
        entries.append({"Key": key, "Value": value if value is not None else ""})
    return entries


def search_hides_row(search_text: str, *fields) -> bool:
    """Returns True when none of the row fields match the search text."""
    needle = search_text.lower()
    return all(needle not in str(field).lower() for field in fields)
