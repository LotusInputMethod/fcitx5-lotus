# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Main window state decisions over config pages, testable without Qt.

The functions duck-type over page objects so the window keeps working with
pages that only implement part of the optional page protocol.
"""

from collections.abc import Iterable


def _pages(pages: Iterable) -> list:
    return list(pages)


def page_unsaved_changes(page) -> bool:
    """Returns True when a page differs from defaults or from its load state."""
    if hasattr(page, "is_modified_from_default") and page.is_modified_from_default():
        return True
    return bool(hasattr(page, "is_modified") and page.is_modified())


def any_page_unsaved_changes(pages: Iterable) -> bool:
    """Returns True when any page would need a reset button enabled."""
    return any(page_unsaved_changes(page) for page in _pages(pages))


def any_page_modified(pages: Iterable) -> bool:
    """Returns True when any page carries unsaved edits relative to load."""
    return any(hasattr(page, "is_modified") and page.is_modified() for page in _pages(pages))


def pages_have_validation_errors(pages: Iterable) -> bool:
    """Returns True when any page reports a validation error."""
    return any(
        hasattr(page, "has_validation_errors") and page.has_validation_errors()
        for page in _pages(pages)
    )


def collect_validation_messages(pages: Iterable) -> list[str]:
    """Collects the non-empty validation messages of all pages in order."""
    messages = []
    for page in _pages(pages):
        if not hasattr(page, "validation_message"):
            continue
        message = page.validation_message()
        if message:
            messages.append(message)
    return messages


def compute_action_states(any_modified: bool, has_errors: bool) -> tuple[bool, bool, bool]:
    """Derives the Apply/Cancel/OK enabled states from the page summary."""
    return any_modified and not has_errors, any_modified, not has_errors


def save_all_pages(pages: Iterable) -> bool:
    """Saves every page that supports it; stops at the first failure."""
    for page in _pages(pages):
        if hasattr(page, "save_data") and page.save_data() is False:
            return False
    return True


def discard_page_changes(pages: Iterable) -> None:
    """Reloads every page from the engine, preferring the load_data hook."""
    for page in _pages(pages):
        if hasattr(page, "load_data"):
            page.load_data()
        elif hasattr(page, "load_config"):
            page.load_config()


def reset_pages_to_defaults(pages: Iterable) -> None:
    """Restores defaults on every page that supports it."""
    for page in _pages(pages):
        if hasattr(page, "restore_defaults"):
            page.restore_defaults()
