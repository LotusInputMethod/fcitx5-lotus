# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared confirmation dialog for restoring factory defaults per section."""

from gi.repository import Adw, Gtk
from i18n import _

_MAX_CHANGES_LIST_HEIGHT = 280


def _build_changes_list(changes: list[tuple[str, str, str]]) -> Gtk.ScrolledWindow | None:
    if not changes:
        return None
    listbox = Gtk.ListBox()
    listbox.set_selection_mode(Gtk.SelectionMode.NONE)
    listbox.add_css_class("boxed-list")

    for item_label, current_display, default_display in changes:
        row = Adw.ActionRow()
        row.set_use_markup(False)
        row.set_title(str(item_label))
        row.set_subtitle(f"{current_display} → {default_display}")
        listbox.append(row)

    scrolled = Gtk.ScrolledWindow()
    scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
    scrolled.set_propagate_natural_height(True)
    scrolled.set_max_content_height(_MAX_CHANGES_LIST_HEIGHT)
    scrolled.set_child(listbox)
    return scrolled


def _configure_dialog_responses(dialog, extra_child, on_confirm) -> None:
    if extra_child is not None:
        dialog.set_extra_child(extra_child)
    dialog.add_response("cancel", _("Cancel"))
    dialog.add_response("reset", _("Reset"))
    dialog.set_response_appearance("reset", Adw.ResponseAppearance.DESTRUCTIVE)
    dialog.set_default_response("cancel")
    dialog.set_close_response("cancel")
    dialog.connect(
        "response",
        lambda _dlg, response: (
            on_confirm() if response == "reset" and callable(on_confirm) else None
        ),
    )


def show_section_reset_dialog(
    parent,
    section_title: str | None,
    changes: list[tuple[str, str, str]],
    on_confirm,
):
    """
    Presents an Adw.AlertDialog confirming factory reset for a section.

    ``changes`` is a list of ``(item_label, current_display, default_display)`` tuples.
    Calls ``on_confirm()`` when the user clicks Reset.
    """
    heading = (
        _("Restore Defaults — {}").format(section_title)
        if section_title
        else _("Restore All Defaults")
    )
    body = (
        _("The following settings will be reset to their factory defaults:")
        if changes
        else _("Restore all modified settings to their factory defaults?")
    )
    extra_child = _build_changes_list(changes)

    if hasattr(Adw, "AlertDialog"):
        dialog = Adw.AlertDialog(heading=heading, body=body)
        _configure_dialog_responses(dialog, extra_child, on_confirm)
        dialog.present(parent)
        return dialog

    transient = (
        parent
        if isinstance(parent, Gtk.Window)
        else (parent.get_root() if hasattr(parent, "get_root") else None)
    )
    dialog = Adw.MessageDialog(transient_for=transient, heading=heading, body=body)
    _configure_dialog_responses(dialog, extra_child, on_confirm)
    dialog.present()
    return dialog
