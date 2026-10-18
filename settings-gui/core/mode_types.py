# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Input mode definitions and mode shortcut logic, testable without Qt."""

from collections.abc import Iterable, Mapping

from core.config_values import config_bool

# Mode constants as defined in C++ LotusEngine
MODE_OFF = 0
MODE_SMOOTH = 1
MODE_SUPER_SMOOTH = 3
MODE_SURROUNDING = 4
MODE_PREEDIT = 5
MODE_EMOJI = 6
MODE_MINECRAFT = 8
MODE_SELECT = 7
MODE_DEFAULT = -1  # UI special value for "Use Global Default"

MODE_INFO = {
    MODE_DEFAULT: {"title": "Default Typing", "icon": "preferences-system"},
    MODE_OFF: {"title": "OFF", "icon": "input-keyboard"},
    MODE_SMOOTH: {"title": "Uinput (Smooth)", "icon": "input-keyboard"},
    MODE_SUPER_SMOOTH: {"title": "Uinput (Super Smooth)", "icon": "input-keyboard"},
    MODE_SURROUNDING: {"title": "Surrounding Text", "icon": "text-field"},
    MODE_PREEDIT: {"title": "Preedit", "icon": "text-field"},
    MODE_EMOJI: {"title": "Emoji Picker", "icon": "face-smile"},
    MODE_MINECRAFT: {"title": "Minecraft", "icon": "onboard"},
    MODE_SELECT: {"title": "Uinput (Select)", "icon": "edit-select"},
}

# Modes offered as the global default and as per-app modes, in display order.
SELECTABLE_MODES = [
    MODE_SMOOTH,
    MODE_SUPER_SMOOTH,
    MODE_MINECRAFT,
    MODE_SELECT,
    MODE_SURROUNDING,
    MODE_PREEDIT,
    MODE_EMOJI,
    MODE_OFF,
]

# Global default delays (config key -> factory default in ms); also the order
# rendered in the "Default Delays" section. 0 in a per-app rule means "use this".
DEFAULT_DELAY_KEYS = {
    "DefaultCommitInterval": 20,
    "DefaultBackspaceInterval": 5,
    "DefaultPostDelay": 10,
}

# Factory value of the ModeOrder config key; unknown entries are kept as-is.
DEFAULT_MODE_ORDER = (
    "Smooth,Uinput,Minecraft,Select,SurroundingText,Preedit,Emoji,Off,SuperSmooth,Default"
)

MODE_SHORTCUT_TO_VISIBILITY = {
    "ShortcutSmooth": "ShowModeSmooth",
    "ShortcutSuperSmooth": "ShowModeSuperSmooth",
    "ShortcutMinecraft": "ShowModeMinecraft",
    "ShortcutSelect": "ShowModeSelect",
    "ShortcutSurroundingText": "ShowModeSurroundingText",
    "ShortcutPreedit": "ShowModePreedit",
    "ShortcutEmoji": "ShowModeEmoji",
    "ShortcutOff": "ShowModeOff",
    "ShortcutDefault": "ShowModeDefault",
}

MODE_KEY_TO_INTERNAL_NAME = {
    "ShortcutSmooth": "Smooth",
    "ShortcutSuperSmooth": "SuperSmooth",
    "ShortcutMinecraft": "Minecraft",
    "ShortcutSelect": "Select",
    "ShortcutSurroundingText": "SurroundingText",
    "ShortcutPreedit": "Preedit",
    "ShortcutEmoji": "Emoji",
    "ShortcutOff": "Off",
    "ShortcutDefault": "Default",
}

MODE_SHORTCUT_KEYS = list(MODE_SHORTCUT_TO_VISIBILITY.keys())


def normalize_mode_order(order_str: str, all_names: Iterable[str]) -> list[str]:
    """Parses the ModeOrder config value and appends any missing mode names."""
    order = order_str.split(",")
    for name in all_names:
        if name not in order:
            order.append(name)
    return order


def enabled_mode_shortcuts(
    values: Mapping,
    metadata: Mapping,
    bool_fn=config_bool,
) -> dict[str, str]:
    """Collects the shortcut values of every mode enabled by its visibility flag.

    Visibility and shortcut defaults come from the config metadata when the
    stored values do not carry the keys; modes without a shortcut are skipped.
    """
    enabled = {}
    for shortcut_key, visibility_key in MODE_SHORTCUT_TO_VISIBILITY.items():
        default_visibility = "True"
        vis_meta = metadata.get(visibility_key)
        if vis_meta:
            default_visibility = vis_meta[3]

        if not bool_fn(values, visibility_key, default_visibility):
            continue

        default_shortcut = ""
        shortcut_meta = metadata.get(shortcut_key)
        if shortcut_meta:
            default_shortcut = shortcut_meta[3]
        val = values.get(shortcut_key, default_shortcut)
        if val:
            enabled[shortcut_key] = val
    return enabled


def is_shortcut_related(key: str) -> bool:
    """Returns True when a config key affects mode shortcuts or visibility."""
    return key in MODE_SHORTCUT_KEYS or key in MODE_SHORTCUT_TO_VISIBILITY.values()
