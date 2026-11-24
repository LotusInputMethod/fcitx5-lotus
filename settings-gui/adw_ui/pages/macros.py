# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Macro Editor Page for configuring shorthand expansions and behavior (Adwaita)."""

import copy

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, Gdk, Gtk, Pango
from i18n import _

from adw_ui.pages.base import StagedPage
from adw_ui.utils import (
    as_bool,
    bool_to_str,
    make_empty_row,
    register_css,
    remove_rows_from_group,
    save_dbus_values,
    set_entry_error,
)
from adw_ui.widgets.section_header import attach_header_suffix_reset, build_list_section_header

SKIP_CHOICES = [
    ("Disabled", _("Disabled")),
    ("Shift", _("Shift")),
    ("Ctrl", _("Ctrl")),
    ("Alt", _("Alt")),
]

TIME_PRESETS = [
    ("%H:%M", "15:04 (24h)"),
    ("%H:%M:%S", "15:04:05 (24h)"),
    ("%I:%M %p", "03:04 PM"),
    ("%I:%M:%S %p", "03:04:05 PM"),
    ("", _("None")),
]

DATE_PRESETS = [
    ("%d/%m/%Y", "dd/MM/yyyy"),
    ("%d/%m/%y", "dd/MM/yy"),
    ("%m/%d/%Y", "MM/dd/yyyy"),
    ("%Y-%m-%d", "yyyy-MM-dd"),
    ("%y-%m-%d", "yy-MM-dd"),
    ("", _("None")),
]

_SKIP_KEYS = [k for k, _ in SKIP_CHOICES]
_SKIP_MAP = dict(SKIP_CHOICES)
_TIME_KEYS = [fmt for fmt, _ in TIME_PRESETS]
_DATE_KEYS = [fmt for fmt, _ in DATE_PRESETS]

_DEFAULT_BEHAVIOR = {
    "EnableMacro": True,
    "CapitalizeMacro": True,
    "EnableMacroInOffMode": False,
    "MacroSkipTriggerModifier": "Disabled",
    "TimeFormat": "%H:%M",
    "DateFormat": "%d/%m/%Y",
}

_MACRO_KEY_MAX_UNELLIPSIZED = 20
_MACRO_KEY_WIDTH_CHARS = 18
_MACRO_KEY_MAX_WIDTH_CHARS = 24
_MACRO_TOOLTIP_LEN_THRESHOLD = 50
_MACRO_DIALOG_WIDTH = 480
_MACRO_DIALOG_HEIGHT = 340
_MACRO_TEXTVIEW_MIN_HEIGHT = 120

_MACRO_CSS = """
.macro-key-col {
    min-width: 72px;
}

.macro-textview-frame {
    border-radius: 10px;
    border: 1px solid alpha(@window_fg_color, 0.16);
    background-color: @view_bg_color;
}

.macro-textview-frame:focus-within {
    border-color: @accent_color;
}

button.macro-token-btn {
    background-color: alpha(@window_fg_color, 0.07);
    border: 1px solid alpha(@window_fg_color, 0.14);
    border-radius: 999px;
    padding: 2px 10px;
    min-height: 24px;
    font-size: 0.85em;
    font-family: monospace;
    font-weight: 600;
}

button.macro-token-btn:hover {
    background-color: alpha(@window_fg_color, 0.12);
}

label.macro-error-label {
    color: @error_color;
    font-size: 0.85em;
}
"""


def _serialize_behavior(state: dict) -> dict[str, str]:
    return {
        "EnableMacro": bool_to_str(bool(state["EnableMacro"])),
        "CapitalizeMacro": bool_to_str(bool(state["CapitalizeMacro"])),
        "EnableMacroInOffMode": bool_to_str(bool(state["EnableMacroInOffMode"])),
        "MacroSkipTriggerModifier": state["MacroSkipTriggerModifier"],
        "TimeFormat": state["TimeFormat"],
        "DateFormat": state["DateFormat"],
    }


class MacroEditorPage(StagedPage):
    """PreferencesPage for configuring Lotus macro behavior and shorthand table."""

    PAGE_ID = "macros"
    SUPPORTS_VALIDATION = True

    def __init__(self, dbus_handler: LotusDBusHandler, on_modified_changed=None, **kwargs):
        super().__init__(dbus_handler, on_modified_changed=on_modified_changed, **kwargs)
        register_css("macros", _MACRO_CSS)

        self.macros = []
        self._initial_state = {}
        self._macro_rows = []
        self._key_size_group = None
        self._search_text = ""

        self._setup_ui()
        self.load_data()

    def _make_behavior_switch(
        self, group: Adw.PreferencesGroup, title: str, tooltip: str
    ) -> Adw.SwitchRow:
        row = Adw.SwitchRow()
        row.set_use_markup(False)
        row.set_title(title)
        row.set_tooltip_text(tooltip)
        row.connect("notify::active", self._on_setting_changed)
        group.add(row)
        return row

    def _make_behavior_combo(
        self, group: Adw.PreferencesGroup, title: str, tooltip: str, labels: list[str]
    ) -> Adw.ComboRow:
        row = Adw.ComboRow()
        row.set_use_markup(False)
        row.set_title(title)
        row.set_tooltip_text(tooltip)
        row.set_model(Gtk.StringList.new(labels))
        row.connect("notify::selected", self._on_setting_changed)
        group.add(row)
        return row

    def _setup_ui(self):
        # Group 1: Behavior
        group_behavior = Adw.PreferencesGroup()
        group_behavior.set_title(_("Behavior"))
        self.btn_reset_behavior = attach_header_suffix_reset(
            group_behavior, self._reset_behavior_section
        )

        self.row_enable = self._make_behavior_switch(
            group_behavior,
            _("Enable Macros"),
            _("Expand shorthand abbreviations while typing"),
        )
        self.row_capitalize = self._make_behavior_switch(
            group_behavior,
            _("Match Case"),
            _(
                "Automatically match expansion case to trigger key case.\n\n"
                "Example if 'kg' is 'Khô gà':\n"
                "- kg -> khô gà\n"
                "- Kg -> Khô gà\n"
                "- KG -> KHÔ GÀ"
            ),
        )
        self.row_off_mode = self._make_behavior_switch(
            group_behavior,
            _("Off-Mode Macros"),
            _(
                "Allow macros to work when the input mode is OFF.\n"
                "When disabled, macros are only available in active typing modes."
            ),
        )
        self.row_skip = self._make_behavior_combo(
            group_behavior,
            _("Skip Modifier"),
            _(
                "Press and release the selected modifier key before typing to skip macro expansion for the next word.\n"
                "If another key is pressed between the modifier's press and release, the skip is cancelled."
            ),
            [label for _, label in SKIP_CHOICES],
        )
        self.row_time = self._make_behavior_combo(
            group_behavior,
            _("Time Format"),
            _("Format used for the $TIME dynamic placeholder"),
            [desc for _, desc in TIME_PRESETS],
        )
        self.row_date = self._make_behavior_combo(
            group_behavior,
            _("Date Format"),
            _("Format used for the $DATE dynamic placeholder"),
            [desc for _, desc in DATE_PRESETS],
        )

        self.add(group_behavior)

        # Group 2: Macros
        self.group_macros = Adw.PreferencesGroup()
        self.group_macros.set_title(_("Macros"))

        controls = build_list_section_header(
            self.group_macros,
            on_reset=self._reset_table_section,
            on_add=lambda _b: self._open_macro_dialog(),
            add_tooltip=_("Add Macro"),
            on_search_changed=self._on_search_changed,
        )
        self.btn_reset_table = controls.btn_reset
        self.search_entry = controls.search_entry
        self.add(self.group_macros)

    @staticmethod
    def _is_invalid_macro(key: str) -> bool:
        return bool(key) and not key.isalnum()

    def _apply_behavior_values(self, state: dict):
        self.row_enable.set_active(bool(state.get("EnableMacro", True)))
        self.row_capitalize.set_active(bool(state.get("CapitalizeMacro", True)))
        self.row_off_mode.set_active(bool(state.get("EnableMacroInOffMode", False)))

        skip_val = state.get("MacroSkipTriggerModifier", "Disabled")
        self.row_skip.set_selected(_SKIP_KEYS.index(skip_val) if skip_val in _SKIP_KEYS else 0)

        time_val = state.get("TimeFormat", "%H:%M")
        self.row_time.set_selected(_TIME_KEYS.index(time_val) if time_val in _TIME_KEYS else 0)

        date_val = state.get("DateFormat", "%d/%m/%Y")
        self.row_date.set_selected(_DATE_KEYS.index(date_val) if date_val in _DATE_KEYS else 0)

    def _get_current_state(self) -> dict:
        return {
            "EnableMacro": self.row_enable.get_active(),
            "CapitalizeMacro": self.row_capitalize.get_active(),
            "EnableMacroInOffMode": self.row_off_mode.get_active(),
            "MacroSkipTriggerModifier": _SKIP_KEYS[self.row_skip.get_selected()],
            "TimeFormat": _TIME_KEYS[self.row_time.get_selected()],
            "DateFormat": _DATE_KEYS[self.row_date.get_selected()],
            "data": [{"Key": m["Key"], "Value": m["Value"]} for m in self.macros],
        }

    def load_data(self):
        """Loads macro settings and table from D-Bus and resets baseline."""
        with self._updating_guard():
            config = self.dbus.get_config()
            values = config.get("values", {}) if config else {}
            self._apply_behavior_values(
                {
                    "EnableMacro": as_bool(values.get("EnableMacro", "True"), default=True),
                    "CapitalizeMacro": as_bool(values.get("CapitalizeMacro", "True"), default=True),
                    "EnableMacroInOffMode": as_bool(
                        values.get("EnableMacroInOffMode", "False"), default=False
                    ),
                    "MacroSkipTriggerModifier": values.get("MacroSkipTriggerModifier", "Disabled"),
                    "TimeFormat": values.get("TimeFormat", "%H:%M"),
                    "DateFormat": values.get("DateFormat", "%d/%m/%Y"),
                }
            )

            self.macros = []
            try:
                data = self.dbus.get_sub_config_list("lotus-macro", "Macro")
                for item in data:
                    key = str(item.get("Key", "")).strip()
                    val = str(item.get("Value", ""))
                    if key:
                        self.macros.append({"Key": key, "Value": val})
            except Exception as e:
                print(f"Error loading macros via DBus: {e}")

            self._sort_macros()
            self._initial_state = copy.deepcopy(self._get_current_state())
            self._refresh_table()

        self._notify_modified()

    def is_modified(self) -> bool:
        """Returns True if staged macro settings or entries differ from baseline."""
        return self._get_current_state() != self._initial_state

    def is_modified_from_default(self) -> bool:
        """Returns True if any macro entry exists or settings differ from factory defaults."""
        cur = self._get_current_state()
        return bool(cur["data"]) or any(cur[k] != v for k, v in _DEFAULT_BEHAVIOR.items())

    def has_validation_errors(self) -> bool:
        """Returns True if any macro key is non-alphanumeric."""
        return any(self._is_invalid_macro(m["Key"]) for m in self.macros)

    def validation_message(self) -> str:
        """Returns the validation error message when any macro key is invalid."""
        if self.has_validation_errors():
            return _("Macro keys cannot contain spaces or special characters.")
        return ""

    def apply_changes(self) -> bool:
        """Persists staged macro settings and table via D-Bus and resets baseline."""
        if self.has_validation_errors():
            return False
        state = self._get_current_state()
        if not save_dbus_values(self.dbus, _serialize_behavior(state)):
            return False

        if not self.dbus.set_sub_config_list("lotus-macro", "Macro", state["data"]):
            return False

        self._initial_state = copy.deepcopy(state)
        self._notify_modified()
        return True

    def cancel_changes(self):
        """Discards staged edits and restores the baseline state."""
        with self._updating_guard():
            self._apply_behavior_values(self._initial_state)
            self.macros = copy.deepcopy(self._initial_state.get("data", []))
            self._refresh_table()
        self._notify_modified()

    def restore_defaults(self):
        """Resets macro settings to factory defaults and clears the macro list."""
        with self._updating_guard():
            self._apply_behavior_values(_DEFAULT_BEHAVIOR)
            self.macros = []
            self._refresh_table()
        self._notify_modified()

    @staticmethod
    def _format_behavior_value(key: str, val) -> str:
        if isinstance(val, bool):
            return _("On") if val else _("Off")
        if key == "MacroSkipTriggerModifier":
            return _SKIP_MAP.get(val, str(val))
        if key in ("TimeFormat", "DateFormat"):
            return str(val) if val else _("None")
        return str(val)

    def _behavior_reset_changes(self) -> list[tuple[str, str, str]]:
        cur = self._get_current_state()
        labels = {
            "EnableMacro": _("Enable Macros"),
            "CapitalizeMacro": _("Match Case"),
            "EnableMacroInOffMode": _("Off-Mode Macros"),
            "MacroSkipTriggerModifier": _("Skip Modifier"),
            "TimeFormat": _("Time Format"),
            "DateFormat": _("Date Format"),
        }
        return [
            (
                labels[k],
                self._format_behavior_value(k, cur[k]),
                self._format_behavior_value(k, default_val),
            )
            for k, default_val in _DEFAULT_BEHAVIOR.items()
            if cur.get(k) != default_val
        ]

    def _table_reset_changes(self) -> list[tuple[str, str, str]]:
        if not self.macros:
            return []
        return [
            (
                _("Macro Entries"),
                _("{count} macros").format(count=len(self.macros)),
                _("Empty (0 macros)"),
            )
        ]

    def get_factory_reset_changes(self) -> list[tuple[str, str, str]]:
        """Returns factory-reset diff rows for global reset confirmation."""
        return self._behavior_reset_changes() + self._table_reset_changes()

    def _reset_behavior_section(self, *_args):
        """Prompts confirmation then factory-resets and persists Behavior settings."""
        changes = self._behavior_reset_changes()

        def apply_reset() -> bool:
            if not save_dbus_values(self.dbus, _serialize_behavior(_DEFAULT_BEHAVIOR)):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self._apply_behavior_values(_DEFAULT_BEHAVIOR)
            for k, v in _DEFAULT_BEHAVIOR.items():
                self._initial_state[k] = v
            return True

        self._prompt_section_reset(_("Behavior"), changes, apply_reset)

    def _reset_table_section(self, *_args):
        """Prompts confirmation then factory-resets and persists macro entries."""
        changes = self._table_reset_changes()

        def apply_reset() -> bool:
            if not self.dbus.set_sub_config_list("lotus-macro", "Macro", []):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self.macros = []
                self._refresh_table()
            self._initial_state["data"] = []
            return True

        self._prompt_section_reset(_("Macros"), changes, apply_reset)

    def _update_section_reset_buttons(self):
        cur = self._get_current_state()
        behavior_changed = any(cur[k] != v for k, v in _DEFAULT_BEHAVIOR.items())
        self.btn_reset_behavior.set_visible(behavior_changed)
        self.btn_reset_table.set_visible(len(self.macros) > 0)

    def _on_setting_changed(self, *_args):
        if self._updating:
            return
        self._notify_modified()

    def _on_search_changed(self, entry):
        self._search_text = entry.get_text().strip().lower()
        self._refresh_table()

    def _sort_macros(self) -> None:
        self.macros.sort(key=lambda m: (m["Key"].casefold(), m["Key"]))

    def _refresh_table(self):
        self._sort_macros()
        remove_rows_from_group(self.group_macros, self._macro_rows)

        filtered = [
            (i, m)
            for i, m in enumerate(self.macros)
            if not self._search_text
            or self._search_text in m["Key"].lower()
            or self._search_text in m["Value"].lower()
        ]

        if not filtered:
            empty_row = make_empty_row(
                _("No Matching Macros") if self._search_text else _("No Macros Defined")
            )
            self.group_macros.add(empty_row)
            self._macro_rows.append(empty_row)
            return

        key_size_group = Gtk.SizeGroup(mode=Gtk.SizeGroupMode.HORIZONTAL)
        self._key_size_group = key_size_group

        for orig_idx, macro in filtered:
            row = self._build_macro_row(orig_idx, macro, key_size_group)
            self.group_macros.add(row)
            self._macro_rows.append(row)

    def _build_macro_row(
        self, orig_idx: int, macro: dict, key_size_group: Gtk.SizeGroup
    ) -> Adw.ActionRow:
        key = macro["Key"]
        val = macro["Value"]
        invalid = self._is_invalid_macro(key)
        preview_val = " ⏎ ".join(line.strip() for line in val.splitlines()) if "\n" in val else val

        row = Adw.ActionRow()
        row.set_use_markup(False)
        row.set_title(preview_val)
        row.set_title_lines(2)
        row.set_activatable(True)
        row.connect("activated", lambda _r, idx=orig_idx: self._open_macro_dialog(idx))

        prefix_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        prefix_box.set_valign(Gtk.Align.CENTER)

        key_col = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        key_col.add_css_class("macro-key-col")
        key_col.set_halign(Gtk.Align.START)
        key_col.set_valign(Gtk.Align.CENTER)
        key_size_group.add_widget(key_col)

        key_badge = Gtk.Label(label=key)
        key_badge.add_css_class("keycap")
        key_badge.add_css_class("macro-key-badge")
        key_badge.add_css_class("monospace")
        key_badge.set_halign(Gtk.Align.START)
        key_badge.set_valign(Gtk.Align.CENTER)
        if len(key) > _MACRO_KEY_MAX_UNELLIPSIZED:
            key_badge.set_width_chars(_MACRO_KEY_WIDTH_CHARS)
            key_badge.set_max_width_chars(_MACRO_KEY_MAX_WIDTH_CHARS)
            key_badge.set_ellipsize(Pango.EllipsizeMode.END)
        else:
            key_badge.set_ellipsize(Pango.EllipsizeMode.NONE)

        if invalid:
            key_badge.add_css_class("error")
            row.add_css_class("error")
            row.set_tooltip_text(_("Macro keys cannot contain spaces or special characters."))
        elif (
            len(val) > _MACRO_TOOLTIP_LEN_THRESHOLD
            or len(key) > _MACRO_KEY_MAX_UNELLIPSIZED
            or "\n" in val
        ):
            row.set_tooltip_text(f"{key} → {val}")

        key_col.append(key_badge)
        prefix_box.append(key_col)

        arrow = Gtk.Label(label="→")
        arrow.add_css_class("dim-label")
        arrow.set_valign(Gtk.Align.CENTER)
        arrow.set_margin_end(4)
        prefix_box.append(arrow)

        row.add_prefix(prefix_box)
        chevron = Gtk.Image.new_from_icon_name("go-next-symbolic")
        chevron.add_css_class("dim-label")
        chevron.set_valign(Gtk.Align.CENTER)
        row.add_suffix(chevron)
        return row

    def _remove_macro(self, index: int):
        if 0 <= index < len(self.macros):
            self.macros.pop(index)
            self._refresh_table()
            self._notify_modified()

    def _make_token_button(self, token: str, tooltip: str) -> Gtk.Button:
        btn = Gtk.Button(label=f"+ {token}")
        btn.add_css_class("flat")
        btn.add_css_class("macro-token-btn")
        btn.set_valign(Gtk.Align.CENTER)
        btn.set_tooltip_text(tooltip)
        return btn

    def _build_macro_dialog_ui(self, title: str, is_edit: bool) -> Adw.Dialog:
        dialog = Adw.Dialog()
        dialog.set_title(title)
        dialog.set_content_width(_MACRO_DIALOG_WIDTH)
        dialog.set_content_height(_MACRO_DIALOG_HEIGHT + (44 if is_edit else 0))

        toolbar_view = Adw.ToolbarView()

        header_bar = Adw.HeaderBar()
        header_bar.add_css_class("flat")
        header_bar.set_title_widget(Adw.WindowTitle.new(title, ""))
        header_bar.set_show_start_title_buttons(False)
        header_bar.set_show_end_title_buttons(False)

        btn_cancel = Gtk.Button(label=_("Cancel"))
        btn_cancel.connect("clicked", lambda _b: dialog.close())
        header_bar.pack_start(btn_cancel)

        btn_save = Gtk.Button(label=_("Save") if is_edit else _("Add"))
        btn_save.add_css_class("suggested-action")
        header_bar.pack_end(btn_save)
        toolbar_view.add_top_bar(header_bar)

        body = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        body.set_margin_top(8)
        body.set_margin_bottom(18)
        body.set_margin_start(18)
        body.set_margin_end(18)

        # Abbreviation section
        key_section = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        lbl_key = Gtk.Label(label=_("Abbreviation"), xalign=0.0)
        lbl_key.add_css_class("heading")
        key_section.append(lbl_key)

        entry_key = Gtk.Entry()
        entry_key.set_placeholder_text(_("e.g. vn, branch, cnow"))
        entry_key.add_css_class("monospace")
        entry_key.set_size_request(-1, 38)
        key_section.append(entry_key)

        err_label = Gtk.Label(
            label=_("Macro keys cannot contain spaces or special characters."),
            xalign=0.0,
        )
        err_label.add_css_class("macro-error-label")
        err_label.set_visible(False)
        key_section.append(err_label)
        body.append(key_section)

        # Expansion section (multi-line TextView + placeholder chips)
        exp_section = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        exp_section.set_vexpand(True)

        exp_header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        lbl_exp = Gtk.Label(label=_("Expansion"), xalign=0.0, hexpand=True)
        lbl_exp.add_css_class("heading")
        lbl_exp.set_valign(Gtk.Align.CENTER)
        exp_header.append(lbl_exp)

        btn_time = self._make_token_button("$TIME", _("Insert $TIME dynamic placeholder"))
        btn_date = self._make_token_button("$DATE", _("Insert $DATE dynamic placeholder"))
        exp_header.append(btn_time)
        exp_header.append(btn_date)
        exp_section.append(exp_header)

        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        scrolled.set_min_content_height(_MACRO_TEXTVIEW_MIN_HEIGHT)
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scrolled.set_overflow(Gtk.Overflow.HIDDEN)
        scrolled.add_css_class("macro-textview-frame")

        text_view = Gtk.TextView()
        text_view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        text_view.set_top_margin(10)
        text_view.set_bottom_margin(10)
        text_view.set_left_margin(12)
        text_view.set_right_margin(12)
        text_view.set_accepts_tab(False)
        scrolled.set_child(text_view)
        exp_section.append(scrolled)
        body.append(exp_section)

        btn_delete = None
        if is_edit:
            btn_delete = Gtk.Button()
            btn_delete.add_css_class("destructive-action")
            btn_delete.set_halign(Gtk.Align.END)
            del_content = Adw.ButtonContent()
            del_content.set_icon_name("user-trash-symbolic")
            del_content.set_label(_("Remove"))
            btn_delete.set_child(del_content)
            body.append(btn_delete)

        toolbar_view.set_content(body)
        dialog.set_child(toolbar_view)

        dialog._entry_key = entry_key
        dialog._text_view = text_view
        dialog._btn_save = btn_save
        dialog._btn_cancel = btn_cancel
        dialog._btn_delete = btn_delete
        dialog._btn_time = btn_time
        dialog._btn_date = btn_date
        dialog._err_label = err_label
        return dialog

    def _open_macro_dialog(self, edit_index: int | None = None):
        is_edit = edit_index is not None and 0 <= edit_index < len(self.macros)
        title = _("Edit Macro") if is_edit else _("Add Macro")
        dialog = self._build_macro_dialog_ui(title, is_edit)

        entry_key = dialog._entry_key
        text_view = dialog._text_view
        btn_save = dialog._btn_save
        err_label = dialog._err_label
        buf = text_view.get_buffer()

        if is_edit:
            entry_key.set_text(self.macros[edit_index]["Key"])
            buf.set_text(self.macros[edit_index]["Value"], -1)

        def get_expansion_text() -> str:
            return buf.get_text(buf.get_start_iter(), buf.get_end_iter(), True).strip()

        def validate(*_args):
            k = entry_key.get_text().strip()
            v = get_expansion_text()
            invalid = self._is_invalid_macro(k)
            set_entry_error(
                entry_key,
                _("Macro keys cannot contain spaces or special characters.") if invalid else "",
            )
            err_label.set_visible(invalid)
            btn_save.set_sensitive(bool(k) and bool(v) and not invalid)

        def insert_token(token: str):
            buf.insert_at_cursor(token, -1)
            text_view.grab_focus()

        def commit_save(*_args):
            k = entry_key.get_text().strip()
            v = get_expansion_text()
            if not k or not v or self._is_invalid_macro(k):
                return

            if is_edit:
                self.macros[edit_index] = {"Key": k, "Value": v}
                for i in range(len(self.macros) - 1, -1, -1):
                    if i != edit_index and self.macros[i]["Key"] == k:
                        self.macros.pop(i)
            else:
                for item in self.macros:
                    if item["Key"] == k:
                        item["Value"] = v
                        break
                else:
                    self.macros.append({"Key": k, "Value": v})

            self._sort_macros()
            self._refresh_table()
            self._notify_modified()
            dialog.close()

        dialog._btn_time.connect("clicked", lambda _b: insert_token("$TIME"))
        dialog._btn_date.connect("clicked", lambda _b: insert_token("$DATE"))
        entry_key.connect("changed", validate)
        buf.connect("changed", validate)
        entry_key.connect(
            "activate",
            lambda _e: commit_save() if btn_save.get_sensitive() else text_view.grab_focus(),
        )
        btn_save.connect("clicked", commit_save)
        if is_edit and dialog._btn_delete is not None:

            def on_delete(*_args):
                self._remove_macro(edit_index)
                dialog.close()

            dialog._btn_delete.connect("clicked", on_delete)

        def on_key_pressed(_ctrl, keyval, _kc, _state):
            if keyval == Gdk.KEY_Escape:
                dialog.close()
                return True
            return False

        key_ctrl = Gtk.EventControllerKey()
        key_ctrl.connect("key-pressed", on_key_pressed)
        dialog.add_controller(key_ctrl)

        validate()
        self._macro_dialog = dialog
        dialog.present(self.get_root())
