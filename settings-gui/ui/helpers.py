# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Helper utilities and shared mappings for the Lotus settings GUI.
"""

from i18n import N_, _

from ui.components import HelpIcon

# Tooltip text for specific settings keys
HELPERS = {
    "FreeMarking": N_("You can type tone marks at the end of the word or anywhere inside."),
    "FixUinputWithAck": N_(
        "Fix typing issues in Uinput mode for Chromium-based browsers like Chrome or Edge."
    ),
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


def add_help_icon(layout, key):
    """
    Utility to add a HelpIcon to a layout based on a setting key.

    The icon is only added when ``key`` has a matching entry in ``HELPERS``.
    Returns the created HelpIcon, or ``None`` if no helper text is available.
    """
    # Only add if we have a mapped helper text
    helper_text = HELPERS.get(key)
    if helper_text:
        icon = HelpIcon(_(helper_text))
        layout.addWidget(icon)
        return icon
    return None
