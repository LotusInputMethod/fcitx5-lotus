# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reusable GTK 4 / Libadwaita widgets for Lotus Settings."""

from adw_ui.widgets.reset_dialog import show_section_reset_dialog
from adw_ui.widgets.section_header import (
    SectionHeaderControls,
    attach_header_suffix_reset,
    build_list_section_header,
    make_section_reset_button,
)
from adw_ui.widgets.shortcut_dialog import (
    ShortcutCaptureDialog,
    create_keycap_box,
    keyval_to_shortcut_string,
    pretty_format_hotkey_parts,
)

__all__ = [
    "SectionHeaderControls",
    "ShortcutCaptureDialog",
    "attach_header_suffix_reset",
    "build_list_section_header",
    "create_keycap_box",
    "keyval_to_shortcut_string",
    "make_section_reset_button",
    "pretty_format_hotkey_parts",
    "show_section_reset_dialog",
]
