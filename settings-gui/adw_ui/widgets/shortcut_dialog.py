# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Interactive shortcut capture dialog and keycap formatting helpers."""

from gi.repository import Adw, Gdk, Gtk
from i18n import _

from adw_ui.utils import clear_children

_DIALOG_WIDTH = 400
_DIALOG_HEIGHT = 270
_ICON_PIXEL_SIZE = 48

MODIFIER_KEYVAL_NAMES = {
    Gdk.KEY_Control_L: "Control",
    Gdk.KEY_Control_R: "Control",
    Gdk.KEY_Alt_L: "Alt",
    Gdk.KEY_Alt_R: "Alt",
    Gdk.KEY_Shift_L: "Shift",
    Gdk.KEY_Shift_R: "Shift",
    Gdk.KEY_Super_L: "Super",
    Gdk.KEY_Super_R: "Super",
    Gdk.KEY_Meta_L: "Super",
    Gdk.KEY_Meta_R: "Super",
    Gdk.KEY_Hyper_L: "Super",
    Gdk.KEY_Hyper_R: "Super",
}

MODIFIER_KEYVALS = set(MODIFIER_KEYVAL_NAMES)

_RELEVANT_MOD_MASK = (
    Gdk.ModifierType.CONTROL_MASK
    | Gdk.ModifierType.ALT_MASK
    | Gdk.ModifierType.SHIFT_MASK
    | Gdk.ModifierType.SUPER_MASK
    | Gdk.ModifierType.META_MASK
)

_NON_SHIFT_MOD_MASK = (
    Gdk.ModifierType.CONTROL_MASK
    | Gdk.ModifierType.ALT_MASK
    | Gdk.ModifierType.SUPER_MASK
    | Gdk.ModifierType.META_MASK
)

_MODIFIER_PREFIXES = (
    ("Shift", ("Shift_",)),
    ("Control", ("Control_", "Control")),
    ("Alt", ("Alt_", "Alt")),
    ("Super", ("Super_", "Meta_")),
)

SPECIAL_SINGLE_KEYS = {
    "grave",
    "space",
    "Tab",
    "Return",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
}

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
    "space": "Space",
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

for _ch in "abcdefghijklmnopqrstuvwxyz":
    HOTKEY_UI_UNSHIFT_MAP[_ch.upper()] = _ch


def pretty_format_hotkey_parts(hotkey_str: str | None) -> list[str]:
    """Converts an Fcitx5 shortcut string into user-friendly keycap labels."""
    if not hotkey_str:
        return []

    if hotkey_str == "+":
        parts = ["+"]
    elif hotkey_str.endswith("++"):
        parts = hotkey_str[:-2].split("+") + ["+"]
    else:
        parts = hotkey_str.split("+")

    raw_mods: list[str] = []
    base_key_part = None

    for part in parts:
        if not part:
            continue
        if part in ("Control", "Alt", "Super", "Shift"):
            if part not in raw_mods:
                raw_mods.append(part)
        else:
            base_key_part = part

    if base_key_part:
        for mod_name, prefixes in _MODIFIER_PREFIXES:
            if mod_name in raw_mods and any(
                base_key_part == p or (p.endswith("_") and base_key_part.startswith(p))
                for p in prefixes
            ):
                raw_mods.remove(mod_name)
                break

    pretty_parts: list[str] = []
    explicit_shift_needed = "Shift" in raw_mods
    has_non_shift_modifier = any(mod in raw_mods for mod in ("Control", "Alt", "Super"))

    if base_key_part:
        if len(base_key_part) == 1 and base_key_part.isupper():
            explicit_shift_needed = explicit_shift_needed or not has_non_shift_modifier
            base_key_label = base_key_part
        elif base_key_part in HOTKEY_UI_UNSHIFT_MAP:
            explicit_shift_needed = True
            display_base = HOTKEY_UI_UNSHIFT_MAP[base_key_part]
            base_key_label = HOTKEY_SYM_MAP.get(display_base, display_base.capitalize())
        else:
            base_key_label = HOTKEY_SYM_MAP.get(base_key_part, base_key_part.capitalize())
    else:
        base_key_label = ""

    for mod_key, mod_label in (
        ("Control", "Ctrl"),
        ("Alt", "Alt"),
        ("Super", "Super"),
    ):
        if mod_key in raw_mods:
            pretty_parts.append(mod_label)
    if explicit_shift_needed:
        pretty_parts.append("Shift")

    if base_key_label:
        pretty_parts.append(base_key_label)

    return pretty_parts


def create_keycap_box(shortcut_str: str | None, empty_label: str | None = None) -> Gtk.Widget:
    """Builds a horizontal box of styled ``.keycap`` labels for a shortcut string."""
    parts = pretty_format_hotkey_parts(shortcut_str)
    if not parts:
        lbl = Gtk.Label(label=empty_label if empty_label is not None else _("Disabled"))
        lbl.add_css_class("dim-label")
        return lbl

    box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
    box.set_valign(Gtk.Align.CENTER)
    for part in parts:
        cap = Gtk.Label(label=part)
        cap.add_css_class("keycap")
        cap.set_valign(Gtk.Align.CENTER)
        box.append(cap)
    return box


def keyval_to_shortcut_string(
    keyval: int,
    state: Gdk.ModifierType,
    single_key_only: bool = False,
) -> str:
    """Formats a Gdk keyval and modifier state into an Fcitx5 shortcut string."""
    name = Gdk.keyval_name(keyval)
    if not name:
        return ""

    if name == "ISO_Left_Tab":
        name = "Tab"

    if single_key_only:
        return name

    has_non_shift = bool(state & _NON_SHIFT_MOD_MASK)
    is_ascii_alpha = len(name) == 1 and name.isascii() and name.isalpha()
    if has_non_shift and is_ascii_alpha:
        name = name.upper()

    parts: list[str] = []
    if state & Gdk.ModifierType.CONTROL_MASK:
        parts.append("Control")
    if state & Gdk.ModifierType.ALT_MASK:
        parts.append("Alt")
    if state & (Gdk.ModifierType.SUPER_MASK | Gdk.ModifierType.META_MASK):
        parts.append("Super")
    if (state & Gdk.ModifierType.SHIFT_MASK) and (
        (has_non_shift and is_ascii_alpha) or (name not in HOTKEY_UI_UNSHIFT_MAP)
    ):
        parts.append("Shift")

    parts.append(name)
    return "+".join(parts)


class ShortcutCaptureDialog(Adw.Dialog):
    """Modal dialog that captures a key or shortcut combination from keyboard input."""

    def __init__(
        self,
        title: str,
        current_shortcut: str = "",
        allow_single_key: bool = True,
        single_key_only: bool = False,
        on_captured=None,
        **kwargs,
    ):
        super().__init__(**kwargs)
        self.set_title(title)
        self.set_content_width(_DIALOG_WIDTH)
        self.set_content_height(_DIALOG_HEIGHT)

        self.allow_single_key = allow_single_key
        self.single_key_only = single_key_only
        self.on_captured = on_captured
        self.captured_shortcut = current_shortcut or ""
        self._held_mods: set[str] = set()

        self._setup_ui()

    def _setup_ui(self):
        toolbar_view = Adw.ToolbarView()

        header_bar = Adw.HeaderBar()
        header_bar.add_css_class("flat")

        btn_cancel = Gtk.Button(label=_("Cancel"))
        btn_cancel.connect("clicked", lambda _b: self.close())
        header_bar.pack_start(btn_cancel)

        btn_clear = Gtk.Button(label=_("Disable"))
        btn_clear.add_css_class("destructive-action")
        btn_clear.connect("clicked", self._on_clear_clicked)
        header_bar.pack_end(btn_clear)

        toolbar_view.add_top_bar(header_bar)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        box.set_valign(Gtk.Align.CENTER)
        box.set_halign(Gtk.Align.CENTER)
        box.set_margin_top(20)
        box.set_margin_bottom(24)
        box.set_margin_start(24)
        box.set_margin_end(24)

        icon = Gtk.Image.new_from_icon_name("preferences-desktop-keyboard-shortcuts-symbolic")
        icon.set_pixel_size(_ICON_PIXEL_SIZE)
        box.append(icon)

        self.label_status = Gtk.Label()
        self.label_status.set_label(
            _("Press a single key…") if self.single_key_only else _("Press shortcut…")
        )
        self.label_status.add_css_class("title-2")
        box.append(self.label_status)

        self.preview_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        self.preview_box.set_halign(Gtk.Align.CENTER)
        self.preview_box.set_valign(Gtk.Align.CENTER)
        box.append(self.preview_box)
        self._refresh_preview()

        self.label_hint = Gtk.Label()
        self.label_hint.set_label(_("Escape to cancel · Backspace to disable"))
        self.label_hint.add_css_class("dim-label")
        box.append(self.label_hint)

        toolbar_view.set_content(box)
        self.set_child(toolbar_view)

        controller = Gtk.EventControllerKey()
        controller.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
        controller.connect("key-pressed", self._on_key_pressed)
        controller.connect("key-released", self._on_key_released)
        self.add_controller(controller)

    def _refresh_preview(self):
        clear_children(self.preview_box)
        if self._held_mods:
            for mod in ("Control", "Alt", "Super", "Shift"):
                if mod in self._held_mods:
                    cap = Gtk.Label(label=HOTKEY_SYM_MAP.get(mod, mod))
                    cap.add_css_class("keycap")
                    self.preview_box.append(cap)
            ellipsis = Gtk.Label(label="…")
            ellipsis.add_css_class("dim-label")
            self.preview_box.append(ellipsis)
        elif self.captured_shortcut:
            self.preview_box.append(create_keycap_box(self.captured_shortcut))
        else:
            lbl = Gtk.Label(label=_("Disabled"))
            lbl.add_css_class("dim-label")
            self.preview_box.append(lbl)

    def _on_key_pressed(self, _controller, keyval, _keycode, state):
        if keyval in MODIFIER_KEYVALS:
            if not self.single_key_only:
                mod = MODIFIER_KEYVAL_NAMES.get(keyval)
                if mod:
                    self._held_mods.add(mod)
                    self._refresh_preview()
            return True

        if keyval == Gdk.KEY_Escape:
            if self.get_mapped():
                self.close()
            return True

        relevant_mods = state & _RELEVANT_MOD_MASK

        if keyval in (Gdk.KEY_BackSpace, Gdk.KEY_Delete) and (
            self.single_key_only or not relevant_mods
        ):
            self._apply_shortcut("")
            return True

        if not self.single_key_only and not self.allow_single_key and not relevant_mods:
            name = Gdk.keyval_name(keyval)
            if name not in SPECIAL_SINGLE_KEYS:
                return False

        shortcut = keyval_to_shortcut_string(
            keyval,
            relevant_mods,
            single_key_only=self.single_key_only,
        )
        if shortcut:
            self._apply_shortcut(shortcut)
            return True

        return False

    def _on_key_released(self, _controller, keyval, _keycode, _state):
        if self.single_key_only:
            return
        mod = MODIFIER_KEYVAL_NAMES.get(keyval)
        if mod and mod in self._held_mods:
            self._held_mods.discard(mod)
            self._refresh_preview()

    def _apply_shortcut(self, shortcut: str):
        self.captured_shortcut = shortcut
        if callable(self.on_captured):
            self.on_captured(shortcut)
        if self.get_mapped():
            self.close()

    def _on_clear_clicked(self, _button):
        self._apply_shortcut("")
