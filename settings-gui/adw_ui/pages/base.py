# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Base class for PreferencesPages with staged edits, validation, and section resets."""

from contextlib import contextmanager

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw
from i18n import _

from adw_ui.utils import show_toast_for
from adw_ui.widgets.reset_dialog import show_section_reset_dialog


class StagedPage(Adw.PreferencesPage):
    """Shared lifecycle and notification plumbing for editable PreferencesPages."""

    PAGE_ID = ""
    SUPPORTS_VALIDATION = False

    def __init__(self, dbus_handler: LotusDBusHandler, on_modified_changed=None, **kwargs):
        super().__init__(**kwargs)
        self.dbus = dbus_handler
        self._on_modified_callback = on_modified_changed
        self._updating = False
        self._reset_dialog = None

    @contextmanager
    def _updating_guard(self):
        """Temporarily suppresses change-handler propagation during programmatic updates."""
        prev = self._updating
        self._updating = True
        try:
            yield
        finally:
            self._updating = prev

    def is_modified(self) -> bool:
        """Returns True if staged page edits differ from the loaded baseline."""
        return False

    def is_modified_from_default(self) -> bool:
        """Returns True if current page state differs from factory defaults."""
        return False

    def has_validation_errors(self) -> bool:
        """Returns True if staged data has validation errors that block Apply."""
        return False

    def validation_message(self) -> str:
        """Returns a human-readable validation error message, or empty string."""
        return ""

    def get_factory_reset_changes(self) -> list[tuple[str, str, str]]:
        """Returns ``(label, current_display, default_display)`` rows for reset confirmation."""
        return []

    def apply_changes(self) -> bool:
        """Persists staged modifications to D-Bus/disk and updates the baseline."""
        return True

    def cancel_changes(self) -> None:
        """Discards staged edits and reverts UI state to the baseline."""

    def restore_defaults(self) -> None:
        """Stages factory default values in memory without immediate persistence."""

    def _update_section_reset_buttons(self) -> None:
        """Updates visibility of section-level reset buttons."""

    def _show_toast(self, text: str) -> None:
        """Displays a toast notification in the enclosing window or overlay."""
        show_toast_for(self, text)

    def _confirm_section_reset(
        self, section_title: str, changes: list[tuple[str, str, str]], on_confirm
    ):
        """Presents the section factory-reset confirmation dialog and keeps a reference."""
        self._reset_dialog = show_section_reset_dialog(
            self.get_root(), section_title, changes, on_confirm
        )
        return self._reset_dialog

    def _prompt_section_reset(
        self,
        section_title: str,
        changes: list[tuple[str, str, str]],
        apply_reset,
    ) -> None:
        """Confirms a section factory reset, then runs ``apply_reset()`` and updates UI."""
        if not changes:
            return

        def do_reset():
            if apply_reset():
                self._notify_modified()
                self._show_toast(_("Restored defaults for {}").format(section_title))

        self._confirm_section_reset(section_title, changes, do_reset)

    def _notify_modified(self) -> None:
        """Refreshes section reset visibility and notifies the parent window of staged state."""
        self._update_section_reset_buttons()
        if not callable(self._on_modified_callback):
            return
        if self.SUPPORTS_VALIDATION:
            self._on_modified_callback(
                self.PAGE_ID,
                self.is_modified(),
                self.apply_changes,
                self.cancel_changes,
                has_error=self.has_validation_errors(),
                error_msg=self.validation_message(),
            )
        else:
            self._on_modified_callback(
                self.PAGE_ID,
                self.is_modified(),
                self.apply_changes,
                self.cancel_changes,
            )
