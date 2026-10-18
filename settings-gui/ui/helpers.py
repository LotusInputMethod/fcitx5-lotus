# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Helper utilities and shared mappings for the Lotus settings GUI.
"""

from core.config_values import as_bool, bool_to_str, config_bool
from core.dict_store import local_dict_path
from i18n import N_, _
from qtpy.QtGui import QIcon

from ui.components import HelpIcon

# Re-exported pure helpers; the canonical implementations live in core.
__all__ = [
    "HELPERS",
    "add_help_icon",
    "as_bool",
    "bool_to_str",
    "config_bool",
    "local_dict_path",
    "notify_parent_changed",
    "set_add_button_mode",
]

# Tooltip text for specific settings keys
HELPERS = {
    "FreeMarking": N_("You can type tone marks at the end of the word or anywhere inside."),
    "CapitalizeMacro": N_(
        "Automatically match expansion case to trigger key case.\n\n"
        "Example if 'kg' is 'Khô gà':\n"
        "- kg -> khô gà\n"
        "- Kg -> Khô gà\n"
        "- KG -> KHÔ GÀ"
    ),
    "AutoNonVnRestore": N_(
        "Automatically revert the typed sequence if the resulting word is not in the dictionary.\n"
        "This helps prevent accidental Vietnamese transformations on English words or mixed text."
    ),
    "EnableMacroInOffMode": N_(
        "Allow macros to work when the input mode is OFF.\n"
        "When disabled, macros are only available in active typing modes."
    ),
    "MacroSkipTriggerModifier": N_(
        "Press and release the selected modifier key before typing to skip macro expansion for the next word.\n"
        "If another key is pressed between the modifier's press and release, the skip is cancelled."
    ),
}


def add_help_icon(layout, key, clear_existing=False):
    """
    Utility to add a HelpIcon to a layout based on a setting key.
    Optionally clears existing HelpIcons from the layout first.
    """
    if clear_existing:
        # Avoid duplicate icons by removing existing ones in the layout cleanly
        for i in reversed(range(layout.count())):
            item = layout.itemAt(i)
            if item and item.widget() and isinstance(item.widget(), HelpIcon):
                widget = layout.takeAt(i).widget()
                if widget:
                    widget.deleteLater()

    # Only add if we have a mapped helper text
    helper_text = HELPERS.get(key)
    if helper_text:
        icon = HelpIcon(_(helper_text))
        layout.addWidget(icon)
        return icon
    return None


def notify_parent_changed(widget) -> None:
    """Notifies the main window that the given widget changed a setting."""
    main_win = widget.window()
    if hasattr(main_win, "on_changed"):
        main_win.on_changed()


def set_add_button_mode(
    button,
    found: bool,
    update_text: str,
    add_text: str,
    update_tip: str = None,
    add_tip: str = None,
) -> None:
    """Switches an editor's add button between the Add and Update states."""
    if found:
        button.setIcon(QIcon.fromTheme("document-save"))
        button.setText(update_text)
        if update_tip is not None:
            button.setToolTip(update_tip)
    else:
        button.setIcon(QIcon.fromTheme("list-add"))
        button.setText(add_text)
        if add_tip is not None:
            button.setToolTip(add_tip)
