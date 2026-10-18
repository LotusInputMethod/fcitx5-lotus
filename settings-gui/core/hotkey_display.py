# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hotkey display formatting, testable without Qt.

Converts internal fcitx5 keysym names into user-friendly key cap labels:
shifted keysyms are mapped back to their base key plus an explicit Shift
label, and redundant modifier labels are de-duplicated.
"""

# Common XKB keysym name to character mapping for display
HOTKEY_SYM_MAP = {
    "grave": "`",
    "minus": "-",
    "equal": "=",
    "bracketleft": "[",
    "bracketright": "]",
    "backslash": "\\",
    "semicolon": ";",
    "apostrophe": "'",
    "comma": ",",
    "period": ".",
    "slash": "/",
    "asciitilde": "~",
    "underscore": "_",
    "plus": "+",
    "braceleft": "{",
    "braceright": "}",
    "bar": "|",
    "colon": ":",
    "quotedbl": '"',
    "less": "<",
    "greater": ">",
    "question": "?",
    "exclam": "!",
    "at": "@",
    "numbersign": "#",
    "dollar": "$",
    "percent": "%",
    "asciicircum": "^",
    "ampersand": "&",
    "asterisk": "*",
    "parenleft": "(",
    "parenright": ")",
    "Control": "Ctrl",
    "Escape": "Esc",
    "Return": "Enter",
    "BackSpace": "Backspace",
    "Delete": "Del",
    "Insert": "Ins",
    "ISO_Left_Tab": "Tab",
    "Shift_L": "L-Shift",
    "Shift_R": "R-Shift",
    "Control_L": "L-Ctrl",
    "Control_R": "R-Ctrl",
    "Alt_L": "L-Alt",
    "Alt_R": "R-Alt",
    "Super_L": "L-Super",
    "Super_R": "R-Super",
    "Meta_L": "L-Super",
    "Meta_R": "R-Super",
}

# Mapping for UI DISPLAY ONLY: Converts shifted keysyms back to base keys + explicit Shift label.
# This keeps the stored string engine-compliant (e.g. 'asciitilde') but UI-friendly (e.g. 'Shift' + '`').
# NOTE: We exclude ISO_Left_Tab here because Fcitx5 configuration prefers explicit 'Shift+Tab' strings.
HOTKEY_UI_UNSHIFT_MAP = {
    "asciitilde": "grave",
    "exclam": "1",
    "at": "2",
    "numbersign": "3",
    "dollar": "4",
    "percent": "5",
    "asciicircum": "6",
    "ampersand": "7",
    "asterisk": "8",
    "parenleft": "9",
    "parenright": "0",
    "underscore": "minus",
    "plus": "equal",
    "braceleft": "bracketleft",
    "braceright": "bracketright",
    "bar": "backslash",
    "colon": "semicolon",
    "quotedbl": "apostrophe",
    "less": "comma",
    "greater": "period",
    "question": "slash",
}

for _char in "abcdefghijklmnopqrstuvwxyz":
    HOTKEY_UI_UNSHIFT_MAP[_char.upper()] = _char


def pretty_format_hotkey_parts(hotkey_str):
    """Converts internal keysym names to user-friendly characters for display as a list.
    Automatically de-normalizes shifted keysyms and de-duplicates redundant modifiers."""
    if not hotkey_str:
        return []

    # Robustly handle hotkeys containing '+' key (e.g. 'Control++')
    if hotkey_str == "+":
        parts = ["+"]
    elif hotkey_str.endswith("++"):
        parts = hotkey_str[:-2].split("+") + ["+"]
    else:
        parts = hotkey_str.split("+")

    raw_mods = []
    base_key_part = None

    # Analyze parts to identify modifiers and the base key
    for part in parts:
        if not part:
            continue
        if part in ("Control", "Alt", "Super", "Shift"):
            if part not in raw_mods:
                raw_mods.append(part)
        else:
            base_key_part = part

    # De-duplicate: If base key IS a specific modifier (e.g. Shift_L),
    # suppress the corresponding generic modifier label.
    if base_key_part:
        if base_key_part.startswith("Shift_"):
            if "Shift" in raw_mods:
                raw_mods.remove("Shift")
        elif base_key_part.startswith("Control_") or base_key_part == "Control":
            if "Control" in raw_mods:
                raw_mods.remove("Control")
        elif base_key_part.startswith("Alt_") or base_key_part == "Alt":
            if "Alt" in raw_mods:
                raw_mods.remove("Alt")
        elif base_key_part.startswith("Super_") or base_key_part.startswith("Meta_"):
            if "Super" in raw_mods:
                raw_mods.remove("Super")

    pretty_parts = []
    explicit_shift_needed = "Shift" in raw_mods
    has_non_shift_modifier = any(mod in raw_mods for mod in ("Control", "Alt", "Super"))

    # Process base key for UI display
    if base_key_part:
        if len(base_key_part) == 1 and base_key_part.isupper():
            explicit_shift_needed = explicit_shift_needed or not has_non_shift_modifier
            base_key_label = base_key_part  # Keep uppercase A-Z
        # Check for symbols that imply Shift (Display only)
        elif base_key_part in HOTKEY_UI_UNSHIFT_MAP:
            explicit_shift_needed = True
            display_base = HOTKEY_UI_UNSHIFT_MAP[base_key_part]
            base_key_label = HOTKEY_SYM_MAP.get(display_base, display_base.capitalize())
        else:
            base_key_label = HOTKEY_SYM_MAP.get(base_key_part, base_key_part.capitalize())
    else:
        base_key_label = ""

    # Build the final ordered list of labels
    if "Control" in raw_mods:
        pretty_parts.append("Ctrl")
    if "Alt" in raw_mods:
        pretty_parts.append("Alt")
    if "Super" in raw_mods:
        pretty_parts.append("Super")
    if explicit_shift_needed:
        pretty_parts.append("Shift")

    if base_key_label:
        pretty_parts.append(base_key_label)

    return pretty_parts
