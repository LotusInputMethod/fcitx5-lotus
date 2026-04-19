# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Helper utilities and shared mappings for the Lotus settings GUI.
"""

from i18n import _
from ui.components import HelpIcon


# Tooltip text for specific settings keys
HELPERS = {
    "FreeMarking": _(
        "You can type tone marks at the end of the word or anywhere inside."
    ),
    "FixUinputWithAck": _(
        "Fix typing issues in Uinput mode for Chromium-based browsers like Chrome or Edge."
    ),
    "CapitalizeMacro": _(
        "Automatically match expansion case to trigger key case.\n\n"
        "Example if 'kg' is 'khô gà':\n"
        "- kg -> khô gà\n"
        "- Kg -> Khô gà\n"
        "- KG -> KHÔ GÀ"
    ),
    "AutoNonVnRestore": _(
        "Automatically revert the typed sequence if the resulting word is not in the dictionary.\n"
        "This helps prevent accidental Vietnamese transformations on English words or mixed text."
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
        icon = HelpIcon(helper_text)
        layout.addWidget(icon)
        return icon
    return None
