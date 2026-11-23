# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Custom Keymap Editor Page for Lotus Input Method (Adwaita)."""

import copy

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, Gtk, Pango
from i18n import _

from adw_ui.pages.base import StagedPage
from adw_ui.utils import (
    as_bool,
    bool_to_str,
    make_delete_button,
    make_empty_row,
    remove_rows_from_group,
    save_dbus_values,
)
from adw_ui.widgets.section_header import attach_header_suffix_reset, build_list_section_header

BAMBOO_ACTIONS = [
    ("XoaDauThanh", "Xóa dấu thanh"),
    ("DauSac", "Dấu sắc"),
    ("DauHuyen", "Dấu huyền"),
    ("DauHoi", "Dấu hỏi"),
    ("DauNga", "Dấu ngã"),
    ("DauNang", "Dấu nặng"),
    ("A_Â", "a -> â"),
    ("E_Ê", "e -> ê"),
    ("O_Ô", "o -> ô"),
    ("AEO_ÂÊÔ", "a/e/o -> â/ê/ô"),
    ("UOA_ƯƠĂ", "u/o/a -> ư/ơ/ă"),
    ("D_Đ", "d -> đ"),
    ("UO_ƯƠ", "u/o -> ư/ơ"),
    ("A_Ă", "a -> ă"),
    ("__ă", "ă"),
    ("_Ă", "Ă"),
    ("__â", "â"),
    ("_Â", "Â"),
    ("__ê", "ê"),
    ("_Ê", "Ê"),
    ("__ô", "ô"),
    ("_Ô", "Ô"),
    ("__ư", "ư"),
    ("_Ư", "Ư"),
    ("__ơ", "ơ"),
    ("_Ơ", "Ơ"),
    ("__đ", "đ"),
    ("_Đ", "Đ"),
    ("UOA_ƯƠĂ__Ư", "u/o/a -> ư/ơ/ă, ư"),
]

PRESETS = {
    "Telex": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ"),
        ("d", "D_Đ"),
    ],
    "VNI": [
        ("0", "XoaDauThanh"),
        ("1", "DauSac"),
        ("2", "DauHuyen"),
        ("3", "DauHoi"),
        ("4", "DauNga"),
        ("5", "DauNang"),
        ("6", "AEO_ÂÊÔ"),
        ("7", "UO_ƯƠ"),
        ("8", "A_Ă"),
        ("9", "D_Đ"),
    ],
    "VIQR": [
        ("0", "XoaDauThanh"),
        ("'", "DauSac"),
        ("`", "DauHuyen"),
        ("?", "DauHoi"),
        ("~", "DauNga"),
        (".", "DauNang"),
        ("^", "AEO_ÂÊÔ"),
        ("+", "UO_ƯƠ"),
        ("*", "UO_ƯƠ"),
        ("(", "A_Ă"),
        ("d", "D_Đ"),
    ],
    "Microsoft layout": [
        ("8", "DauSac"),
        ("5", "DauHuyen"),
        ("6", "DauHoi"),
        ("7", "DauNga"),
        ("9", "DauNang"),
        ("1", "__ă"),
        ("!", "_Ă"),
        ("2", "__â"),
        ("@", "_Â"),
        ("3", "__ê"),
        ("#", "_Ê"),
        ("4", "__ô"),
        ("$", "_Ô"),
        ("0", "__đ"),
        (")", "_Đ"),
    ],
    "Telex + VNI": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ"),
        ("d", "D_Đ"),
        ("0", "XoaDauThanh"),
        ("1", "DauSac"),
        ("2", "DauHuyen"),
        ("3", "DauHoi"),
        ("4", "DauNga"),
        ("5", "DauNang"),
        ("6", "AEO_ÂÊÔ"),
        ("7", "UO_ƯƠ"),
        ("8", "A_Ă"),
        ("9", "D_Đ"),
    ],
    "Telex + VNI + VIQR": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ"),
        ("d", "D_Đ"),
        ("0", "XoaDauThanh"),
        ("1", "DauSac"),
        ("2", "DauHuyen"),
        ("3", "DauHoi"),
        ("4", "DauNga"),
        ("5", "DauNang"),
        ("6", "AEO_ÂÊÔ"),
        ("7", "UO_ƯƠ"),
        ("8", "A_Ă"),
        ("9", "D_Đ"),
        ("'", "DauSac"),
        ("`", "DauHuyen"),
        ("?", "DauHoi"),
        ("~", "DauNga"),
        (".", "DauNang"),
        ("^", "AEO_ÂÊÔ"),
        ("+", "UO_ƯƠ"),
        ("*", "UO_ƯƠ"),
        ("(", "A_Ă"),
        ("\\\\", "D_Đ"),
    ],
    "VNI Bàn phím tiếng Pháp": [
        ("&", "XoaDauThanh"),
        ("é", "DauSac"),
        ('"', "DauHuyen"),
        ("'", "DauHoi"),
        ("(", "DauNga"),
        ("-", "DauNang"),
        ("è", "AEO_ÂÊÔ"),
        ("_", "UO_ƯƠ"),
        ("ç", "A_Ă"),
        ("à", "D_Đ"),
    ],
    "Telex W": [
        ("z", "XoaDauThanh"),
        ("s", "DauSac"),
        ("f", "DauHuyen"),
        ("r", "DauHoi"),
        ("x", "DauNga"),
        ("j", "DauNang"),
        ("a", "A_Â"),
        ("e", "E_Ê"),
        ("o", "O_Ô"),
        ("w", "UOA_ƯƠĂ__Ư"),
        ("d", "D_Đ"),
    ],
}

_PRESET_NAMES = list(PRESETS.keys())
_ACTION_CODES = [code for code, _ in BAMBOO_ACTIONS]
_ACTION_NAMES = [name for _, name in BAMBOO_ACTIONS]
_ACTION_MAP = dict(BAMBOO_ACTIONS)

_KEY_BADGE_WIDTH = 34
_KEY_BADGE_HEIGHT = 28


class KeymapEditorPage(StagedPage):
    """PreferencesPage for configuring custom Bamboo keymap bindings."""

    PAGE_ID = "keymap"

    def __init__(self, dbus_handler: LotusDBusHandler, on_modified_changed=None, **kwargs):
        super().__init__(dbus_handler, on_modified_changed=on_modified_changed, **kwargs)

        self.keymaps = []
        self._initial_state = {}
        self._binding_rows = []
        self._search_text = ""

        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        # Group 1: Configuration
        group_config = Adw.PreferencesGroup()
        group_config.set_title(_("Configuration"))
        self.btn_reset_config = attach_header_suffix_reset(group_config, self._reset_config_section)

        self.row_enable = Adw.SwitchRow()
        self.row_enable.set_use_markup(False)
        self.row_enable.set_title(_("Custom Keymap"))
        self.row_enable.set_tooltip_text(
            _("Use custom key-to-action bindings when Input Method is Custom")
        )
        self.row_enable.connect("notify::active", self._on_enable_toggled)
        group_config.add(self.row_enable)

        self.row_preset = Adw.ComboRow()
        self.row_preset.set_use_markup(False)
        self.row_preset.set_title(_("Preset"))
        self.row_preset.set_tooltip_text(_("Load bindings from a built-in layout"))
        self.row_preset.set_model(Gtk.StringList.new(_PRESET_NAMES))

        btn_apply_preset = Gtk.Button(label=_("Apply"))
        btn_apply_preset.set_valign(Gtk.Align.CENTER)
        btn_apply_preset.set_tooltip_text(_("Replace bindings with the selected preset"))
        btn_apply_preset.connect("clicked", self._on_apply_preset_clicked)
        self.row_preset.add_suffix(btn_apply_preset)
        group_config.add(self.row_preset)

        self.add(group_config)

        # Group 2: Bindings
        self.group_bindings = Adw.PreferencesGroup()
        self.group_bindings.set_title(_("Bindings"))

        controls = build_list_section_header(
            self.group_bindings,
            on_reset=self._reset_bindings_section,
            on_add=self._on_add_binding_clicked,
            add_tooltip=_("Add Binding"),
            on_search_changed=self._on_search_changed,
        )
        self.btn_reset_bindings = controls.btn_reset
        self.search_entry = controls.search_entry
        self.add(self.group_bindings)

    def _get_current_state(self) -> dict:
        return {
            "EnableCustomKeymap": self.row_enable.get_active(),
            "data": [{"Key": km["Key"], "Value": km["Value"]} for km in self.keymaps],
        }

    def load_data(self):
        """Loads EnableCustomKeymap and custom keymap bindings via D-Bus."""
        with self._updating_guard():
            config = self.dbus.get_config()
            values = config.get("values", {}) if config else {}
            self.row_enable.set_active(
                as_bool(values.get("EnableCustomKeymap", "False"), default=False)
            )

            self.keymaps = []
            try:
                data = self.dbus.get_sub_config_list("custom_keymap", "CustomKeymap")
                for item in data:
                    k = str(item.get("Key", "")).strip()
                    v = str(item.get("Value", ""))
                    if k:
                        self.keymaps.append({"Key": k, "Value": v})
            except Exception as e:
                print(f"Error loading custom keymap via DBus: {e}")

            self._initial_state = copy.deepcopy(self._get_current_state())
            self._refresh_bindings()

        self._notify_modified()

    def is_modified(self) -> bool:
        """Returns True if staged keymap toggle or bindings differ from baseline."""
        return self._get_current_state() != self._initial_state

    def is_modified_from_default(self) -> bool:
        """Returns True if custom keymap is enabled or has entries."""
        return self.row_enable.get_active() or len(self.keymaps) > 0

    def apply_changes(self) -> bool:
        """Writes EnableCustomKeymap and CustomKeymap list via D-Bus and resets baseline."""
        state = self._get_current_state()
        if not save_dbus_values(
            self.dbus, {"EnableCustomKeymap": bool_to_str(state["EnableCustomKeymap"])}
        ):
            return False

        if not self.dbus.set_sub_config_list("custom_keymap", "CustomKeymap", state["data"]):
            return False

        self._initial_state = copy.deepcopy(state)
        self._notify_modified()
        return True

    def cancel_changes(self):
        """Discards staged keymap changes and restores the baseline state."""
        with self._updating_guard():
            self.row_enable.set_active(bool(self._initial_state.get("EnableCustomKeymap", False)))
            self.keymaps = copy.deepcopy(self._initial_state.get("data", []))
            self._refresh_bindings()
        self._notify_modified()

    def restore_defaults(self):
        """Disables custom keymap and clears bindings in staged memory."""
        with self._updating_guard():
            self.row_enable.set_active(False)
            self.keymaps = []
            self._refresh_bindings()
        self._notify_modified()

    def _config_reset_changes(self) -> list[tuple[str, str, str]]:
        if not self.row_enable.get_active():
            return []
        return [(_("Custom Keymap"), _("On"), _("Off"))]

    def _bindings_reset_changes(self) -> list[tuple[str, str, str]]:
        if not self.keymaps:
            return []
        return [
            (
                _("Key Bindings"),
                _("{count} bindings").format(count=len(self.keymaps)),
                _("Empty (0 bindings)"),
            )
        ]

    def get_factory_reset_changes(self) -> list[tuple[str, str, str]]:
        """Returns factory-reset diff rows for global reset confirmation."""
        return self._config_reset_changes() + self._bindings_reset_changes()

    def _reset_config_section(self, *_args):
        """Prompts confirmation then factory-resets and persists EnableCustomKeymap."""
        changes = self._config_reset_changes()

        def apply_reset() -> bool:
            if not save_dbus_values(self.dbus, {"EnableCustomKeymap": "False"}):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self.row_enable.set_active(False)
            self._initial_state["EnableCustomKeymap"] = False
            return True

        self._prompt_section_reset(_("Configuration"), changes, apply_reset)

    def _reset_bindings_section(self, *_args):
        """Prompts confirmation then factory-resets and persists keymap bindings."""
        changes = self._bindings_reset_changes()

        def apply_reset() -> bool:
            if not self.dbus.set_sub_config_list("custom_keymap", "CustomKeymap", []):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self.keymaps = []
                self._refresh_bindings()
            self._initial_state["data"] = []
            return True

        self._prompt_section_reset(_("Bindings"), changes, apply_reset)

    def _update_section_reset_buttons(self):
        self.btn_reset_config.set_visible(self.row_enable.get_active())
        self.btn_reset_bindings.set_visible(len(self.keymaps) > 0)

    def _on_enable_toggled(self, *_args):
        if self._updating:
            return
        self._notify_modified()

    def _on_search_changed(self, entry):
        self._search_text = entry.get_text().strip().lower()
        self._refresh_bindings()

    def _refresh_bindings(self):
        remove_rows_from_group(self.group_bindings, self._binding_rows)

        filtered = []
        for idx, km in enumerate(self.keymaps):
            action_name = _ACTION_MAP.get(km["Value"], km["Value"])
            if self._search_text and (
                self._search_text not in km["Key"].lower()
                and self._search_text not in action_name.lower()
                and self._search_text not in km["Value"].lower()
            ):
                continue
            filtered.append((idx, km))

        if not filtered:
            empty_row = make_empty_row(
                _("No Matching Bindings") if self._search_text else _("No Bindings")
            )
            self.group_bindings.add(empty_row)
            self._binding_rows.append(empty_row)
            return

        for orig_idx, km in filtered:
            row = Adw.ActionRow()
            row.set_use_markup(False)
            row.set_title("")

            key_badge = Gtk.Label(label=km["Key"])
            key_badge.add_css_class("keycap")
            key_badge.add_css_class("monospace")
            key_badge.set_size_request(_KEY_BADGE_WIDTH, _KEY_BADGE_HEIGHT)
            key_badge.set_valign(Gtk.Align.CENTER)
            key_badge.set_ellipsize(Pango.EllipsizeMode.END)
            row.add_prefix(key_badge)

            dropdown = Gtk.DropDown()
            dropdown.set_model(Gtk.StringList.new(_ACTION_NAMES))
            dropdown.set_valign(Gtk.Align.CENTER)
            if km["Value"] in _ACTION_CODES:
                dropdown.set_selected(_ACTION_CODES.index(km["Value"]))
            dropdown.connect(
                "notify::selected",
                lambda dd, _pspec, idx=orig_idx: self._on_binding_action_changed(
                    idx, dd.get_selected()
                ),
            )
            row.add_suffix(dropdown)

            row.add_suffix(make_delete_button(lambda _b, idx=orig_idx: self._remove_binding(idx)))

            self.group_bindings.add(row)
            self._binding_rows.append(row)

    def _on_binding_action_changed(self, index: int, selected_pos: int):
        if self._updating:
            return
        if 0 <= index < len(self.keymaps) and 0 <= selected_pos < len(_ACTION_CODES):
            self.keymaps[index]["Value"] = _ACTION_CODES[selected_pos]
            self._notify_modified()

    def _remove_binding(self, index: int):
        if 0 <= index < len(self.keymaps):
            self.keymaps.pop(index)
            self._refresh_bindings()
            self._notify_modified()

    def _on_apply_preset_clicked(self, _button):
        preset_name = _PRESET_NAMES[self.row_preset.get_selected()]
        if not self.keymaps:
            self._apply_preset(preset_name)
            return

        dialog = Adw.AlertDialog(
            heading=_("Apply Preset?"),
            body=_("This operation will replace all existing keys with {}. Are you sure?").format(
                preset_name
            ),
        )
        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("apply", _("Apply"))
        dialog.set_response_appearance("apply", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_default_response("cancel")
        dialog.set_close_response("cancel")
        dialog.connect(
            "response",
            lambda _dlg, response: self._apply_preset(preset_name) if response == "apply" else None,
        )
        dialog.present(self.get_root())

    def _apply_preset(self, preset_name: str):
        self.keymaps = [{"Key": k, "Value": v} for k, v in PRESETS.get(preset_name, [])]
        self._refresh_bindings()
        self._notify_modified()

    def _on_add_binding_clicked(self, _button):
        dialog = Adw.AlertDialog(
            heading=_("Add Binding"),
            body=_("Enter a trigger key and select its action:"),
        )

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)

        entry_key = Gtk.Entry()
        entry_key.set_placeholder_text(_("Key (e.g. s)"))
        entry_key.set_max_length(1)
        entry_key.set_activates_default(True)
        box.append(entry_key)

        combo_action = Gtk.DropDown()
        combo_action.set_model(Gtk.StringList.new(_ACTION_NAMES))
        box.append(combo_action)

        dialog.set_extra_child(box)
        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("add", _("Add"))
        dialog.set_response_appearance("add", Adw.ResponseAppearance.SUGGESTED)
        dialog.set_default_response("add")
        dialog.set_close_response("cancel")
        dialog.set_response_enabled("add", False)

        entry_key.connect(
            "changed",
            lambda ent: dialog.set_response_enabled("add", bool(ent.get_text().strip())),
        )

        def on_response(_dlg, response):
            if response != "add":
                return
            k = entry_key.get_text().strip()
            if not k:
                return
            act_code = _ACTION_CODES[combo_action.get_selected()]
            for item in self.keymaps:
                if item["Key"] == k:
                    item["Value"] = act_code
                    break
            else:
                self.keymaps.append({"Key": k, "Value": act_code})
            self._refresh_bindings()
            self._notify_modified()

        dialog.connect("response", on_response)
        dialog.present(self.get_root())
