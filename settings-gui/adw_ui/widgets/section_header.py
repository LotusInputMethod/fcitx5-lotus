# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared PreferencesGroup header-suffix builders (section reset, search, actions)."""

from dataclasses import dataclass

from gi.repository import Adw, Gtk
from i18n import _

from adw_ui.utils import make_icon_button

_SEARCH_WIDTH_CHARS = 14
_SEARCH_MAX_WIDTH_CHARS = 18


@dataclass
class SectionHeaderControls:
    """References to widgets placed in a list section's PreferencesGroup header."""

    box: Gtk.Box
    btn_reset: Gtk.Button
    search_entry: Gtk.SearchEntry | None
    btn_add: Gtk.Button


def make_section_reset_button(on_clicked) -> Gtk.Button:
    """Creates a compact, initially hidden factory-reset undo button."""
    btn = make_icon_button(
        "edit-undo-symbolic",
        _("Restore section defaults"),
        on_clicked=on_clicked,
    )
    btn.set_visible(False)
    return btn


def attach_header_suffix_reset(group: Adw.PreferencesGroup, on_reset) -> Gtk.Button:
    """Attaches a section factory-reset button to a PreferencesGroup header suffix."""
    btn_reset = make_section_reset_button(lambda _b: on_reset())
    existing = group.get_header_suffix()
    if existing is not None:
        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        box.set_valign(Gtk.Align.CENTER)
        group.set_header_suffix(None)
        box.append(btn_reset)
        box.append(existing)
        group.set_header_suffix(box)
    else:
        group.set_header_suffix(btn_reset)
    return btn_reset


def build_list_section_header(
    group: Adw.PreferencesGroup,
    on_reset,
    on_add,
    add_tooltip: str,
    on_search_changed=None,
    prefix_widgets: list[Gtk.Widget] | None = None,
    pre_add_widgets: list[Gtk.Widget] | None = None,
    search_hidden: bool = False,
) -> SectionHeaderControls:
    """Populates a PreferencesGroup header suffix with reset, optional search, and add controls."""
    header_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
    header_box.set_valign(Gtk.Align.CENTER)

    btn_reset = make_section_reset_button(on_reset)
    header_box.append(btn_reset)

    for widget in prefix_widgets or ():
        header_box.append(widget)

    search_entry = None
    if on_search_changed is not None:
        search_entry = Gtk.SearchEntry()
        search_entry.set_placeholder_text(_("Search…"))
        search_entry.set_width_chars(_SEARCH_WIDTH_CHARS)
        search_entry.set_max_width_chars(_SEARCH_MAX_WIDTH_CHARS)
        search_entry.set_valign(Gtk.Align.CENTER)
        search_entry.connect("search-changed", on_search_changed)
        if search_hidden:
            search_entry.set_visible(False)
        header_box.append(search_entry)

    for widget in pre_add_widgets or ():
        header_box.append(widget)

    btn_add = make_icon_button("list-add-symbolic", add_tooltip, on_clicked=on_add)
    header_box.append(btn_add)

    group.set_header_suffix(header_box)
    return SectionHeaderControls(
        box=header_box,
        btn_reset=btn_reset,
        search_entry=search_entry,
        btn_add=btn_add,
    )
