# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shortcuts PreferencesPage with keycap pills and duplicate validation."""

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, GLib, Gtk
from i18n import _

from adw_ui.pages.base import StagedPage
from adw_ui.utils import (
    as_bool,
    bool_to_str,
    make_icon_button,
    remove_rows_from_group,
    save_dbus_values,
)
from adw_ui.widgets.section_header import attach_header_suffix_reset
from adw_ui.widgets.shortcut_dialog import (
    ShortcutCaptureDialog,
    create_keycap_box,
    pretty_format_hotkey_parts,
)

# (internal_name, shortcut_key, show_key, title, default_key)
# Matches upstream/dev mode list in src/lotus-mode-menu.h and dynamic_settings.py.
MODE_DEFS = [
    ("Smooth", "ShortcutSmooth", "ShowModeSmooth", _("Uinput (Smooth)"), "1"),
    ("SuperSmooth", "ShortcutSuperSmooth", "ShowModeSuperSmooth", _("Uinput (Super Smooth)"), "a"),
    (
        "SurroundingText",
        "ShortcutSurroundingText",
        "ShowModeSurroundingText",
        _("Surrounding Text"),
        "4",
    ),
    ("Preedit", "ShortcutPreedit", "ShowModePreedit", _("Preedit"), "q"),
    ("Emoji", "ShortcutEmoji", "ShowModeEmoji", _("Emoji Picker"), "w"),
    ("Off", "ShortcutOff", "ShowModeOff", _("OFF"), "e"),
    ("Default", "ShortcutDefault", "ShowModeDefault", _("Default Typing"), "r"),
]

DEFAULT_MODE_ORDER = "Smooth,SurroundingText,Preedit,Emoji,Off,SuperSmooth,Default"

_MAIN_SHORTCUT_DEFAULTS = {
    "ModeMenuKey": "grave",
    "CycleModeKey": "",
}
_WARNING_ICON_SIZE = 14


def _extract_hotkey(value, fallback: str = "") -> str:
    if value is None:
        return fallback
    if isinstance(value, dict):
        if "0" in value:
            return str(value.get("0") or "")
        for val in value.values():
            if val:
                return str(val)
        return ""
    return str(value or "")


def _format_shortcut_display(shortcut_str: str) -> str:
    if not shortcut_str:
        return _("Disabled")
    parts = pretty_format_hotkey_parts(shortcut_str)
    return "+".join(parts) if parts else shortcut_str


class ShortcutsPage(StagedPage):
    """PreferencesPage for configuring main hotkeys, mode order, and mode shortcuts."""

    PAGE_ID = "shortcuts"
    SUPPORTS_VALIDATION = True

    def __init__(self, dbus_handler: LotusDBusHandler, on_modified_changed=None, **kwargs):
        super().__init__(dbus_handler, on_modified_changed=on_modified_changed, **kwargs)
        self.config_values = {}
        self.metadata_defaults = {}
        self._pending_render_id = 0

        self.all_mode_names = [m[0] for m in MODE_DEFS]
        self.mode_defs_by_name = {
            m[0]: {
                "name": m[0],
                "shortcut_key": m[1],
                "show_key": m[2],
                "title": m[3],
                "default_key": m[4],
            }
            for m in MODE_DEFS
        }

        self.enabled_modes: list[str] = []
        self.disabled_modes: list[str] = []
        self.mode_widgets: dict[str, dict] = {}
        self._current_enabled_rows: list[Adw.ActionRow] = []
        self._current_disabled_rows: list[Adw.ActionRow] = []

        self.initial_order: list[str] = []
        self.initial_visibility: dict[str, bool] = {}
        self.initial_shortcuts: dict[str, str] = {}
        self.current_shortcuts: dict[str, str] = {}
        self.initial_main_shortcuts: dict[str, str] = {}
        self.current_main_shortcuts: dict[str, str] = {}

        self.connect("destroy", self._on_destroy)
        self._setup_ui()
        self.load_data()

    def _on_destroy(self, *_args) -> None:
        if self._pending_render_id:
            GLib.source_remove(self._pending_render_id)
            self._pending_render_id = 0

    def _make_main_shortcut_row(
        self, group: Adw.PreferencesGroup, config_key: str, title: str
    ) -> tuple[Adw.ActionRow, Gtk.Button]:
        row = Adw.ActionRow()
        row.set_use_markup(False)
        row.set_title(title)
        btn = Gtk.Button()
        btn.add_css_class("shortcut-pill")
        btn.set_valign(Gtk.Align.CENTER)
        btn.connect(
            "clicked",
            lambda _b: self._capture_main_shortcut(config_key, btn, title),
        )
        row.add_suffix(btn)
        row.set_activatable_widget(btn)
        group.add(row)
        return row, btn

    def _setup_ui(self):
        # 1. Main Shortcuts
        group_main = Adw.PreferencesGroup()
        group_main.set_title(_("Main Shortcuts"))
        self.btn_reset_main = attach_header_suffix_reset(group_main, self._on_reset_main_clicked)

        self.row_mode_menu, self.btn_mode_menu = self._make_main_shortcut_row(
            group_main, "ModeMenuKey", _("Mode Menu Hotkey")
        )
        self.row_cycle_mode, self.btn_cycle_mode = self._make_main_shortcut_row(
            group_main, "CycleModeKey", _("Cycle Mode Hotkey")
        )
        self.add(group_main)

        # 2. Mode Switching (Enabled & Reorderable)
        self.group_modes = Adw.PreferencesGroup()
        self.group_modes.set_title(_("Mode Menu Switching"))
        self.group_modes.set_description(_("Active modes and quick-select keys"))
        self.btn_reset_modes = attach_header_suffix_reset(
            self.group_modes, self._on_reset_modes_clicked
        )
        self.add(self.group_modes)

        # 3. Disabled Modes
        self.group_disabled = Adw.PreferencesGroup()
        self.group_disabled.set_title(_("Disabled Modes"))
        self.add(self.group_disabled)

        for name in self.all_mode_names:
            m_def = self.mode_defs_by_name[name]
            row = Adw.ActionRow()
            row.set_use_markup(False)
            row.set_title(m_def["title"])

            box_arrows = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=2)
            box_arrows.set_valign(Gtk.Align.CENTER)

            btn_up = make_icon_button(
                "go-up-symbolic",
                _("Move Up"),
                on_clicked=lambda _b, n=name: self._move_mode_up(n),
            )
            btn_down = make_icon_button(
                "go-down-symbolic",
                _("Move Down"),
                on_clicked=lambda _b, n=name: self._move_mode_down(n),
            )
            box_arrows.append(btn_up)
            box_arrows.append(btn_down)

            warning_badge = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
            warning_badge.add_css_class("warning-badge")
            warning_badge.set_valign(Gtk.Align.CENTER)
            warning_icon = Gtk.Image.new_from_icon_name("dialog-warning-symbolic")
            warning_icon.set_pixel_size(_WARNING_ICON_SIZE)
            warning_label = Gtk.Label(label=_("Duplicate"))
            warning_badge.append(warning_icon)
            warning_badge.append(warning_label)
            warning_badge.set_visible(False)
            row.add_suffix(warning_badge)
            row.add_suffix(box_arrows)

            btn_key = Gtk.Button()
            btn_key.add_css_class("shortcut-pill")
            btn_key.set_valign(Gtk.Align.CENTER)
            btn_key.set_tooltip_text(_("Mode Shortcut"))
            btn_key.connect(
                "clicked",
                lambda _b, n=name, t=m_def["title"], btn=btn_key: self._capture_mode_key(n, btn, t),
            )
            row.add_suffix(btn_key)

            switch = Gtk.Switch()
            switch.set_valign(Gtk.Align.CENTER)
            switch.connect(
                "notify::active",
                lambda sw, _pspec, n=name: self._on_switch_toggled(n, sw.get_active()),
            )
            row.add_suffix(switch)

            self.mode_widgets[name] = {
                "row": row,
                "box_arrows": box_arrows,
                "btn_up": btn_up,
                "btn_down": btn_down,
                "warning_badge": warning_badge,
                "btn_key": btn_key,
                "switch": switch,
            }

    def _update_button_display(self, button: Gtk.Button, shortcut_str: str):
        button.set_child(create_keycap_box(shortcut_str, empty_label=_("Disabled")))

    def _capture_main_shortcut(self, config_key: str, button: Gtk.Button, title: str):
        curr_text = self.current_main_shortcuts.get(config_key, "")

        def on_captured(new_shortcut: str):
            self.current_main_shortcuts[config_key] = new_shortcut
            self._update_button_display(button, new_shortcut)
            self._check_modified()

        dlg = ShortcutCaptureDialog(
            title=title,
            current_shortcut=curr_text,
            allow_single_key=True,
            single_key_only=False,
            on_captured=on_captured,
        )
        dlg.present(self.get_root())

    def _capture_mode_key(self, name: str, button: Gtk.Button, title: str):
        curr_text = self.current_shortcuts.get(name, "")

        def on_captured(new_shortcut: str):
            self.current_shortcuts[name] = new_shortcut
            self._update_button_display(button, new_shortcut)
            self._check_modified()

        dlg = ShortcutCaptureDialog(
            title=title,
            current_shortcut=curr_text,
            allow_single_key=True,
            single_key_only=True,
            on_captured=on_captured,
        )
        dlg.present(self.get_root())

    def _move_mode(self, name: str, delta: int) -> None:
        if name not in self.enabled_modes:
            return
        idx = self.enabled_modes.index(name)
        target = idx + delta
        if 0 <= target < len(self.enabled_modes):
            self.enabled_modes[idx], self.enabled_modes[target] = (
                self.enabled_modes[target],
                self.enabled_modes[idx],
            )
            self._render_mode_rows()
            self._check_modified()

    def _move_mode_up(self, name: str):
        self._move_mode(name, -1)

    def _move_mode_down(self, name: str):
        self._move_mode(name, 1)

    def _on_switch_toggled(self, name: str, is_active: bool):
        if self._updating:
            return

        if is_active:
            if name in self.disabled_modes:
                self.disabled_modes.remove(name)
            if name not in self.enabled_modes:
                self.enabled_modes.append(name)
        else:
            if name in self.enabled_modes:
                self.enabled_modes.remove(name)
            if name not in self.disabled_modes:
                self.disabled_modes.append(name)

        if self._pending_render_id:
            GLib.source_remove(self._pending_render_id)
        self._pending_render_id = GLib.idle_add(self._on_idle_render)
        self._check_modified()

    def _on_idle_render(self) -> bool:
        self._pending_render_id = 0
        self._render_mode_rows()
        return GLib.SOURCE_REMOVE

    def _validate_mode_shortcuts(self) -> tuple[bool, str]:
        """Checks for duplicate shortcuts among enabled modes and highlights conflicts."""
        for name in self.all_mode_names:
            widgets = self.mode_widgets[name]
            widgets["row"].remove_css_class("error")
            widgets["row"].set_subtitle("")
            widgets["btn_key"].remove_css_class("error")
            widgets["warning_badge"].set_visible(False)
            widgets["warning_badge"].set_tooltip_text("")

        by_shortcut: dict[str, list[str]] = {}
        for name in self.enabled_modes:
            shortcut = self.current_shortcuts.get(name, "")
            if shortcut:
                by_shortcut.setdefault(shortcut, []).append(name)

        errors: list[str] = []
        for shortcut, names in by_shortcut.items():
            if len(names) <= 1:
                continue
            msg = _("Duplicate shortcut '{}' used for multiple enabled modes.").format(shortcut)
            errors.append(msg)
            for name in names:
                widgets = self.mode_widgets[name]
                widgets["row"].add_css_class("error")
                widgets["row"].set_subtitle(_("Duplicate key '{}'").format(shortcut))
                widgets["btn_key"].add_css_class("error")
                widgets["warning_badge"].set_tooltip_text(msg)
                widgets["warning_badge"].set_visible(True)

        return (bool(errors), "\n".join(errors))

    def _get_main_default(self, key: str) -> str:
        fallback = _MAIN_SHORTCUT_DEFAULTS.get(key, "")
        if key in self.metadata_defaults:
            return _extract_hotkey(self.metadata_defaults[key], fallback)
        return fallback

    def _get_default_order(self) -> list[str]:
        default_order_str = str(self.metadata_defaults.get("ModeOrder", DEFAULT_MODE_ORDER))
        parsed_order = [
            m.strip()
            for m in default_order_str.split(",")
            if m.strip() and m.strip() in self.mode_defs_by_name
        ]
        for name in self.all_mode_names:
            if name not in parsed_order:
                parsed_order.append(name)
        return parsed_order

    def _get_default_visibility(self, name: str) -> bool:
        show_key = self.mode_defs_by_name[name]["show_key"]
        return as_bool(self.metadata_defaults.get(show_key, "True"), default=True)

    def _get_default_shortcut(self, name: str) -> str:
        m_def = self.mode_defs_by_name[name]
        return str(self.metadata_defaults.get(m_def["shortcut_key"], m_def["default_key"]))

    def _is_main_section_modified(self) -> bool:
        return any(
            self.current_main_shortcuts.get(k) != self.initial_main_shortcuts.get(k)
            for k in _MAIN_SHORTCUT_DEFAULTS
        )

    def _is_modes_section_modified(self) -> bool:
        if (self.enabled_modes + self.disabled_modes) != self.initial_order:
            return True
        for name in self.all_mode_names:
            if (name in self.enabled_modes) != self.initial_visibility.get(name):
                return True
            if self.current_shortcuts.get(name) != self.initial_shortcuts.get(name):
                return True
        return False

    def _is_main_section_non_default(self) -> bool:
        return any(
            self.current_main_shortcuts.get(k, "") != self._get_main_default(k)
            for k in _MAIN_SHORTCUT_DEFAULTS
        )

    def _is_modes_section_non_default(self) -> bool:
        if (self.enabled_modes + self.disabled_modes) != self._get_default_order():
            return True
        for name in self.all_mode_names:
            if (name in self.enabled_modes) != self._get_default_visibility(name):
                return True
            if self.current_shortcuts.get(name, "") != self._get_default_shortcut(name):
                return True
        return False

    def _collect_main_factory_changes(self) -> list[tuple[str, str, str]]:
        changes: list[tuple[str, str, str]] = []
        for key, label in (
            ("ModeMenuKey", _("Mode Menu Hotkey")),
            ("CycleModeKey", _("Cycle Mode Hotkey")),
        ):
            cur = self.current_main_shortcuts.get(key, "")
            default = self._get_main_default(key)
            if cur != default:
                changes.append(
                    (label, _format_shortcut_display(cur), _format_shortcut_display(default))
                )
        return changes

    def _collect_modes_factory_changes(self) -> list[tuple[str, str, str]]:
        changes: list[tuple[str, str, str]] = []
        default_order = self._get_default_order()
        if (self.enabled_modes + self.disabled_modes) != default_order:
            changes.append((_("Order"), _("Custom"), _("Default")))

        for name in self.all_mode_names:
            title = self.mode_defs_by_name[name]["title"]
            is_vis = name in self.enabled_modes
            def_vis = self._get_default_visibility(name)
            if is_vis != def_vis:
                changes.append(
                    (
                        title,
                        _("Visible") if is_vis else _("Hidden"),
                        _("Visible") if def_vis else _("Hidden"),
                    )
                )

            cur_key = self.current_shortcuts.get(name, "")
            def_key = self._get_default_shortcut(name)
            if cur_key != def_key:
                changes.append(
                    (title, _format_shortcut_display(cur_key), _format_shortcut_display(def_key))
                )
        return changes

    def get_factory_reset_changes(self) -> list[tuple[str, str, str]]:
        """Returns ``(label, current, default)`` tuples for all non-default shortcut settings."""
        return self._collect_main_factory_changes() + self._collect_modes_factory_changes()

    def is_modified_from_default(self) -> bool:
        """Returns True if any main hotkey or mode-menu setting differs from factory defaults."""
        return self._is_main_section_non_default() or self._is_modes_section_non_default()

    def _on_reset_main_clicked(self):
        changes = self._collect_main_factory_changes()
        if not changes:
            self._check_modified()
            return
        self._prompt_section_reset(_("Main Shortcuts"), changes, self._apply_main_factory_reset)

    def _on_reset_modes_clicked(self):
        changes = self._collect_modes_factory_changes()
        if not changes:
            self._check_modified()
            return
        self._prompt_section_reset(
            _("Mode Menu Switching"), changes, self._apply_modes_factory_reset
        )

    def _apply_main_factory_reset(self) -> bool:
        def_menu = self._get_main_default("ModeMenuKey")
        def_cycle = self._get_main_default("CycleModeKey")
        updates = {
            "ModeMenuKey": {"0": def_menu} if def_menu else {},
            "CycleModeKey": {"0": def_cycle} if def_cycle else {},
        }
        if not save_dbus_values(self.dbus, updates):
            self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
            return False

        self.initial_main_shortcuts = {
            "ModeMenuKey": def_menu,
            "CycleModeKey": def_cycle,
        }
        self._revert_main_section()
        return True

    def _apply_modes_factory_reset(self) -> bool:
        default_order = self._get_default_order()
        default_vis = {name: self._get_default_visibility(name) for name in self.all_mode_names}
        default_shortcuts = {name: self._get_default_shortcut(name) for name in self.all_mode_names}

        updates: dict = {"ModeOrder": ",".join(default_order)}
        for name in self.all_mode_names:
            m_def = self.mode_defs_by_name[name]
            updates[m_def["show_key"]] = bool_to_str(default_vis[name])
            updates[m_def["shortcut_key"]] = default_shortcuts[name]

        if not save_dbus_values(self.dbus, updates):
            self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
            return False

        self.initial_order = list(default_order)
        self.initial_visibility = dict(default_vis)
        self.initial_shortcuts = dict(default_shortcuts)
        self._revert_modes_section()
        return True

    def _revert_main_section(self):
        self.current_main_shortcuts = dict(self.initial_main_shortcuts)
        self._update_button_display(
            self.btn_mode_menu, self.current_main_shortcuts.get("ModeMenuKey", "")
        )
        self._update_button_display(
            self.btn_cycle_mode, self.current_main_shortcuts.get("CycleModeKey", "")
        )

    def _revert_modes_section(self):
        self.current_shortcuts = dict(self.initial_shortcuts)
        for name in self.all_mode_names:
            self._update_button_display(
                self.mode_widgets[name]["btn_key"],
                self.current_shortcuts.get(name, ""),
            )

        self.enabled_modes = [m for m in self.initial_order if self.initial_visibility.get(m, True)]
        self.disabled_modes = [
            m for m in self.initial_order if not self.initial_visibility.get(m, True)
        ]
        self._render_mode_rows()

    def is_modified(self) -> bool:
        """Returns True if any main hotkey, mode order, visibility, or shortcut was edited."""
        return self._is_main_section_modified() or self._is_modes_section_modified()

    def has_validation_errors(self) -> bool:
        """Returns True when enabled modes contain duplicate shortcut keys."""
        has_error, _ = self._validate_mode_shortcuts()
        return has_error

    def validation_message(self) -> str:
        """Returns the current shortcut validation error message, if any."""
        _has_error, msg = self._validate_mode_shortcuts()
        return msg

    def _update_section_reset_buttons(self) -> None:
        self.btn_reset_main.set_visible(self._is_main_section_non_default())
        self.btn_reset_modes.set_visible(self._is_modes_section_non_default())

    def _check_modified(self):
        self._notify_modified()

    def apply_changes(self) -> bool:
        """Applies pending shortcut modifications to Fcitx5 and updates baseline."""
        has_error, _ = self._validate_mode_shortcuts()
        if has_error:
            return False

        updates: dict = {}

        for k, v in self.current_main_shortcuts.items():
            if v != self.initial_main_shortcuts.get(k):
                updates[k] = {"0": v} if v else {}

        for name in self.all_mode_names:
            curr_key = self.current_shortcuts.get(name, "")
            if curr_key != self.initial_shortcuts.get(name):
                sk = self.mode_defs_by_name[name]["shortcut_key"]
                updates[sk] = curr_key

        for name in self.all_mode_names:
            is_enabled = name in self.enabled_modes
            if is_enabled != self.initial_visibility.get(name):
                show_key = self.mode_defs_by_name[name]["show_key"]
                updates[show_key] = bool_to_str(is_enabled)

        current_order = self.enabled_modes + self.disabled_modes
        if current_order != self.initial_order:
            updates["ModeOrder"] = ",".join(current_order)

        if updates and not save_dbus_values(self.dbus, updates):
            return False

        self.initial_order = list(current_order)
        self.initial_visibility = {
            name: (name in self.enabled_modes) for name in self.all_mode_names
        }
        self.initial_shortcuts = dict(self.current_shortcuts)
        self.initial_main_shortcuts = dict(self.current_main_shortcuts)
        self._check_modified()
        return True

    def cancel_changes(self):
        """Discards pending shortcut modifications and reverts to baseline."""
        self._revert_main_section()
        self._revert_modes_section()
        self._check_modified()

    def restore_defaults(self):
        """Stages factory default values for all shortcuts and mode order."""
        default_order = self._get_default_order()
        self.enabled_modes = [name for name in default_order if self._get_default_visibility(name)]
        self.disabled_modes = [
            name for name in default_order if not self._get_default_visibility(name)
        ]

        self.current_shortcuts = {
            name: self._get_default_shortcut(name) for name in self.all_mode_names
        }
        for name in self.all_mode_names:
            self._update_button_display(
                self.mode_widgets[name]["btn_key"],
                self.current_shortcuts[name],
            )

        default_menu = self._get_main_default("ModeMenuKey")
        default_cycle = self._get_main_default("CycleModeKey")
        self.current_main_shortcuts = {
            "ModeMenuKey": default_menu,
            "CycleModeKey": default_cycle,
        }
        self._update_button_display(self.btn_mode_menu, default_menu)
        self._update_button_display(self.btn_cycle_mode, default_cycle)

        self._render_mode_rows()
        self._check_modified()

    def _render_mode_rows(self):
        if self._pending_render_id:
            GLib.source_remove(self._pending_render_id)
            self._pending_render_id = 0

        remove_rows_from_group(self.group_modes, self._current_enabled_rows)
        remove_rows_from_group(self.group_disabled, self._current_disabled_rows)

        n_enabled = len(self.enabled_modes)
        for i, name in enumerate(self.enabled_modes):
            data = self.mode_widgets[name]
            row = data["row"]
            data["box_arrows"].set_visible(True)
            data["btn_up"].set_sensitive(i > 0)
            data["btn_down"].set_sensitive(i < n_enabled - 1)
            data["btn_key"].set_sensitive(True)

            with self._updating_guard():
                data["switch"].set_active(True)

            self.group_modes.add(row)
            self._current_enabled_rows.append(row)

        self.group_disabled.set_visible(bool(self.disabled_modes))
        for name in self.disabled_modes:
            data = self.mode_widgets[name]
            row = data["row"]
            data["box_arrows"].set_visible(False)
            data["btn_key"].set_sensitive(False)

            with self._updating_guard():
                data["switch"].set_active(False)

            self.group_disabled.add(row)
            self._current_disabled_rows.append(row)

    def load_data(self):
        """Loads shortcut configuration and mode ordering from D-Bus."""
        with self._updating_guard():
            config = self.dbus.get_config()
            self.config_values = config.get("values", {}) if config else {}

            self.metadata_defaults = {}
            raw_meta = config.get("metadata", []) if config else []
            if isinstance(raw_meta, list):
                for section in raw_meta:
                    if len(section) >= 2 and isinstance(section[1], list):
                        for item in section[1]:
                            if len(item) >= 4:
                                self.metadata_defaults[item[0]] = item[3]

            for key, btn in (
                ("ModeMenuKey", self.btn_mode_menu),
                ("CycleModeKey", self.btn_cycle_mode),
            ):
                if key in self.config_values:
                    shortcut = _extract_hotkey(self.config_values[key], "")
                else:
                    shortcut = self._get_main_default(key)
                self._update_button_display(btn, shortcut)
                self.initial_main_shortcuts[key] = shortcut
            self.current_main_shortcuts = dict(self.initial_main_shortcuts)

            order_str = str(
                self.config_values.get(
                    "ModeOrder",
                    self.metadata_defaults.get("ModeOrder", DEFAULT_MODE_ORDER),
                )
            )
            parsed_order = [m.strip() for m in order_str.split(",") if m.strip()]
            for name in self.all_mode_names:
                if name not in parsed_order:
                    parsed_order.append(name)

            self.enabled_modes = []
            self.disabled_modes = []
            for name in parsed_order:
                if name not in self.mode_defs_by_name:
                    continue
                show_key = self.mode_defs_by_name[name]["show_key"]
                default_vis = self.metadata_defaults.get(show_key, "True")
                is_shown = as_bool(self.config_values.get(show_key, default_vis), default=True)
                if is_shown:
                    self.enabled_modes.append(name)
                else:
                    self.disabled_modes.append(name)

            self.initial_order = list(self.enabled_modes + self.disabled_modes)
            self.initial_visibility = {
                name: (name in self.enabled_modes) for name in self.all_mode_names
            }

            for name in self.all_mode_names:
                m_def = self.mode_defs_by_name[name]
                default_key = self.metadata_defaults.get(
                    m_def["shortcut_key"], m_def["default_key"]
                )
                key_val = str(self.config_values.get(m_def["shortcut_key"], default_key))
                self._update_button_display(self.mode_widgets[name]["btn_key"], key_val)
                self.initial_shortcuts[name] = key_val

            self.current_shortcuts = dict(self.initial_shortcuts)
            self._render_mode_rows()
            self._check_modified()
