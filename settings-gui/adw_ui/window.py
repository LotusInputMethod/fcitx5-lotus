# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Main Libadwaita preferences window for Lotus Input Method."""

from collections.abc import Callable

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, Gdk, GLib, Gtk
from i18n import N_, _

from adw_ui.pages.extra import show_about_dialog
from adw_ui.utils import as_bool, bool_to_str, get_explicit_method, save_dbus_values
from adw_ui.widgets.reset_dialog import show_section_reset_dialog
from adw_ui.widgets.section_header import attach_header_suffix_reset

_DEFAULT_WINDOW_WIDTH = 960
_DEFAULT_WINDOW_HEIGHT = 680
_MIN_WINDOW_WIDTH = 640
_MIN_WINDOW_HEIGHT = 460
_MIN_SIDEBAR_WIDTH = 210
_MAX_SIDEBAR_WIDTH = 260
_FLOATING_BAR_DEFAULT_MARGIN = 18
_FLOATING_BAR_TOAST_MARGIN = 76
_REVEAL_ANIMATION_MS = 200

# Tooltip explanations kept off row subtitles to keep rows visually uncluttered.
HELPERS = {
    "FreeMarking": N_("You can type tone marks at the end of the word or anywhere inside."),
    "AutoNonVnRestore": N_(
        "Automatically revert the typed sequence if the resulting word is not in the dictionary.\n"
        "This helps prevent accidental Vietnamese transformations on English words or mixed text."
    ),
    "PreferVirtualKeyboard": N_(
        "Prefer injecting keys through the system instead of the uinput service: "
        "the wlroots virtual keyboard on Wayland and Xtest on X11.\n"
        "Turn this off to always use the uinput service; when neither backend is "
        "available the uinput service is used anyway."
    ),
    "DdFreeStyle": N_("Allow dd to produce đ when Auto Restore Invalid Words is enabled."),
    "BracketTransform": N_("Transform [ to ơ, ] to ư, { to Ơ, and } to Ư."),
    "ModernStyle": N_("Place tone marks on newer vowel positions (oà, uý instead of òa, úy)."),
    "AutoCapitalizeAfterPunctuation": N_(
        "Capitalize the next word after sentence-ending punctuation (. ! ? Enter)."
    ),
    "DoubleSpaceToPeriod": N_("Insert a period followed by a space when pressing Space twice."),
    "DoubleHyphenToEmDash": N_("Transform -- into an em-dash (—)."),
}

FALLBACK_INPUT_METHODS = [
    ("Telex", "Telex"),
    ("VNI", "VNI"),
    ("Telex + VNI", "Telex + VNI"),
    ("Telex + VNI + VIQR", "Telex + VNI + VIQR"),
    ("VIQR", "VIQR"),
    ("Microsoft layout", "Microsoft layout"),
    ("VNI Bàn phím tiếng Pháp", "VNI Bàn phím tiếng Pháp"),
    ("Custom", "Custom"),
]

FALLBACK_MODES = [
    ("OFF", N_("OFF")),
    ("Uinput (Smooth)", N_("Uinput (Smooth)")),
    ("Uinput (Super Smooth)", N_("Uinput (Super Smooth)")),
    ("Surrounding Text", N_("Surrounding Text")),
    ("Preedit", N_("Preedit")),
    ("Emoji Picker", N_("Emoji Picker")),
]

FALLBACK_CHARSETS = [
    ("Unicode", "Unicode"),
    ("BKHCM 1", "BKHCM 1"),
    ("BKHCM 2", "BKHCM 2"),
    ("NCR Decimal", "NCR Decimal"),
    ("NCR Hex", "NCR Hex"),
    ("TCVN3 (ABC)", "TCVN3 (ABC)"),
    ("UTF-8", "UTF-8"),
    ("Unicode C string Decimal", "Unicode C string Decimal"),
    ("Unicode C string Hex", "Unicode C string Hex"),
    ("Unicode tổ hợp", "Unicode tổ hợp"),
    ("VIQR", "VIQR"),
    ("VISCII", "VISCII"),
    ("VNI Windows", "VNI Windows"),
    ("VPS", "VPS"),
    ("Vietware Full", "Vietware Full"),
    ("Vietware X", "Vietware X"),
    ("Windows 1258 codepage", "Windows 1258 codepage"),
]

FALLBACK_TRIGGERS = [
    ("Disabled", N_("Disabled")),
    ("Non-Start", N_("Non-Start")),
    ("Everywhere", N_("Everywhere")),
]

FALLBACK_ICON_THEMES = [
    ("Auto", N_("Auto")),
    ("Light", N_("Light")),
    ("Dark", N_("Dark")),
]

FALLBACK_DEFAULTS = {
    "InputMethod": "Telex",
    "Mode": "Preedit",
    "OutputCharset": "Unicode",
    "W2U": "Non-Start",
    "BracketTransform": "Disabled",
    "UseLotusIcons": "False",
    "IconTheme": "Auto",
    "PreferVirtualKeyboard": "True",
    "SpellCheck": "True",
    "AutoNonVnRestore": "True",
    "DdFreeStyle": "True",
    "ModernStyle": "True",
    "FreeMarking": "True",
    "AutoCapitalizeAfterPunctuation": "False",
    "DoubleSpaceToPeriod": "False",
    "DoubleHyphenToEmDash": "False",
}


class LotusPreferencesWindow(Adw.ApplicationWindow):
    """Libadwaita window with left sidebar navigation and unified staged saving."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.set_title(_("Lotus Settings"))
        self.set_default_size(_DEFAULT_WINDOW_WIDTH, _DEFAULT_WINDOW_HEIGHT)
        self.set_size_request(_MIN_WINDOW_WIDTH, _MIN_WINDOW_HEIGHT)

        self.dbus_handler = LotusDBusHandler()
        self.config_data = self.dbus_handler.get_config() or {}

        self.metadata_map: dict[str, dict] = {}
        self._parse_metadata()

        self.initial_values = dict(self.config_data.get("values", {}))
        self.current_values = dict(self.initial_values)
        self.pending_window_values: dict[str, str] = {}
        self.registered_switches: dict[str, tuple[Adw.SwitchRow, bool]] = {}
        self.registered_combos: dict[str, tuple[Adw.ComboRow, list[str], dict[str, str], str]] = {}

        self.modified_pages: set[str] = set()
        self._page_apply_callbacks: dict[str, Callable] = {}
        self._page_cancel_callbacks: dict[str, Callable] = {}
        self._page_errors: dict[str, str] = {}
        self.subpages: list = []
        self._sidebar_rows: dict[str, Gtk.ListBoxRow] = {}
        self.section_reset_entries: list[tuple[str, list[str], Gtk.Button]] = []
        self._active_toast = None
        self._save_toast = None
        self._visible_toasts: set = set()
        self._loading_settings = False
        self._force_close = False
        self._close_dialog = None

        self._setup_ui()
        self.connect("close-request", self._on_close_request)
        self.on_changed()

    def _parse_metadata(self) -> None:
        self.metadata_map.clear()
        raw_meta = self.config_data.get("metadata", [])
        if isinstance(raw_meta, list):
            for section in raw_meta:
                if len(section) >= 2 and isinstance(section[1], list):
                    for item in section[1]:
                        if len(item) >= 5:
                            self.metadata_map[item[0]] = {
                                "type": item[1],
                                "title": item[2],
                                "default": item[3],
                                "extra": item[4] if isinstance(item[4], dict) else {},
                            }

    def add_toast(self, toast: Adw.Toast, is_save_toast: bool = False) -> None:
        """Displays a toast notification centered over the content pane."""
        self._active_toast = toast
        if is_save_toast:
            self._save_toast = toast
        self._visible_toasts.add(toast)
        toast.connect("dismissed", self._on_toast_dismissed)
        self._update_floating_bar_offset()
        self.toast_overlay.add_toast(toast)

    def _on_toast_dismissed(self, toast: Adw.Toast) -> None:
        self._visible_toasts.discard(toast)
        if self._active_toast is toast:
            self._active_toast = None
        if self._save_toast is toast:
            self._save_toast = None
        self._update_floating_bar_offset()

    def _dismiss_toast(self, toast: Adw.Toast | None) -> None:
        if toast is None:
            return
        self._visible_toasts.discard(toast)
        if self._active_toast is toast:
            self._active_toast = None
        if self._save_toast is toast:
            self._save_toast = None
        self._update_floating_bar_offset()
        toast.dismiss()

    def _update_floating_bar_offset(self) -> None:
        if hasattr(self, "action_revealer"):
            margin = (
                _FLOATING_BAR_TOAST_MARGIN if self._visible_toasts else _FLOATING_BAR_DEFAULT_MARGIN
            )
            self.action_revealer.set_margin_bottom(margin)

    def _get_default_value(self, key: str, fallback=None):
        meta = self.metadata_map.get(key)
        if meta and "default" in meta and meta["default"] is not None:
            raw_default = meta["default"]
            if isinstance(raw_default, bool):
                return bool_to_str(raw_default)
            return str(raw_default)
        if key in FALLBACK_DEFAULTS:
            return FALLBACK_DEFAULTS[key]
        if isinstance(fallback, bool):
            return bool_to_str(fallback)
        return fallback

    def _get_effective_initial(self, key: str, fallback):
        if key in self.initial_values:
            return self.initial_values[key]
        return self._get_default_value(key, fallback)

    def _get_value(self, key: str, default=None):
        if key in self.current_values:
            return self.current_values[key]
        return self._get_default_value(key, default)

    def _get_enum_options(
        self,
        key: str,
        fallback: list[tuple[str, str]],
        translate_labels: bool = True,
    ) -> list[tuple[str, str]]:
        extra = self.metadata_map.get(key, {}).get("extra", {})
        enum_dict = extra.get("Enum", {})
        enum_i18n = extra.get("EnumI18n", {})

        if isinstance(enum_dict, dict) and enum_dict:
            sorted_keys = sorted(
                enum_dict.keys(),
                key=lambda k: (0, int(k)) if str(k).isdigit() else (1, str(k)),
            )
            options = []
            for idx_key in sorted_keys:
                raw = str(enum_dict[idx_key])
                if idx_key in enum_i18n:
                    label = str(enum_i18n[idx_key])
                else:
                    label = _(raw) if translate_labels else raw
                options.append((raw, label))
            return options

        if isinstance(enum_dict, list) and enum_dict:
            return [
                (str(item), _(str(item)) if translate_labels else str(item)) for item in enum_dict
            ]

        return [(raw, _(label) if translate_labels else label) for raw, label in fallback]

    def _on_setting_changed(self, key: str, val, default_fallback=None):
        if self._loading_settings:
            return
        stored_val = bool_to_str(val) if isinstance(val, bool) else str(val)

        self.current_values[key] = stored_val
        initial = self._get_effective_initial(key, default_fallback)
        if isinstance(val, bool):
            same_as_initial = as_bool(initial, default=bool(default_fallback)) == val
        else:
            same_as_initial = str(initial) == stored_val

        if same_as_initial:
            self.pending_window_values.pop(key, None)
        else:
            self.pending_window_values[key] = stored_val
        self.on_changed()

    def set_page_modified(
        self,
        page_id: str,
        is_modified: bool,
        on_apply=None,
        on_cancel=None,
        has_error: bool = False,
        error_msg: str = "",
    ):
        """Registers modified and validation state from a subpage."""
        if is_modified:
            self.modified_pages.add(page_id)
            if on_apply is not None:
                self._page_apply_callbacks[page_id] = on_apply
            if on_cancel is not None:
                self._page_cancel_callbacks[page_id] = on_cancel
        else:
            self.modified_pages.discard(page_id)
            self._page_apply_callbacks.pop(page_id, None)
            self._page_cancel_callbacks.pop(page_id, None)

        if has_error:
            self._page_errors[page_id] = error_msg or _("Resolve validation errors before saving.")
        else:
            self._page_errors.pop(page_id, None)

        self.on_changed()

    def on_changed(self):
        """Updates floating action pill visibility, sensitivity, and section reset visibility."""
        has_changes = bool(self.pending_window_values) or bool(self.modified_pages)
        has_errors = bool(self._page_errors)
        was_revealed = self.action_revealer.get_visible()

        if has_changes:
            if self._save_toast is not None:
                self._dismiss_toast(self._save_toast)
            elif not was_revealed and self._active_toast is not None:
                self._dismiss_toast(self._active_toast)

        self.action_revealer.set_visible(has_changes)
        self.action_revealer.set_reveal_child(has_changes)
        self.action_revealer.set_can_target(has_changes)
        self._update_floating_bar_offset()
        self.btn_apply.set_sensitive(has_changes and not has_errors)
        self.btn_cancel.set_sensitive(has_changes)

        if has_errors:
            err_tip = "\n".join(msg for msg in self._page_errors.values() if msg)
            self.btn_apply.set_tooltip_text(err_tip)
        else:
            self.btn_apply.set_tooltip_text("")

        for _title, keys, btn_reset in self.section_reset_entries:
            section_modified = any(self._is_key_modified_from_default(k) for k in keys)
            btn_reset.set_visible(section_modified)

    def _is_key_modified_from_default(self, key: str) -> bool:
        if key in self.registered_switches:
            _row, fallback = self.registered_switches[key]
            cur_val = as_bool(
                self.current_values.get(key, self._get_default_value(key, fallback)),
                default=bool(fallback),
            )
            def_val = as_bool(
                self._get_default_value(key, fallback),
                default=bool(fallback),
            )
            return cur_val != def_val
        if key in self.registered_combos:
            _row, _options, _label_map, fallback = self.registered_combos[key]
            cur_val = str(self.current_values.get(key, self._get_default_value(key, fallback)))
            def_val = str(self._get_default_value(key, fallback))
            return cur_val != def_val
        return False

    def _collect_section_factory_changes(self, keys: list[str]) -> list[tuple[str, str, str]]:
        changes: list[tuple[str, str, str]] = []
        for key in keys:
            if not self._is_key_modified_from_default(key):
                continue
            if key in self.registered_switches:
                row, fallback = self.registered_switches[key]
                cur_bool = as_bool(
                    self.current_values.get(key, self._get_default_value(key, fallback)),
                    default=bool(fallback),
                )
                def_bool = as_bool(
                    self._get_default_value(key, fallback),
                    default=bool(fallback),
                )
                changes.append(
                    (
                        row.get_title(),
                        _("On") if cur_bool else _("Off"),
                        _("On") if def_bool else _("Off"),
                    )
                )
            elif key in self.registered_combos:
                row, _options, label_map, fallback = self.registered_combos[key]
                cur_val = str(self.current_values.get(key, self._get_default_value(key, fallback)))
                def_val = str(self._get_default_value(key, fallback))
                changes.append(
                    (
                        row.get_title(),
                        label_map.get(cur_val, cur_val),
                        label_map.get(def_val, def_val),
                    )
                )
        return changes

    def _attach_section_reset(
        self, group: Adw.PreferencesGroup, section_title: str, keys: list[str]
    ) -> Gtk.Button:
        """Adds a dynamic factory-reset button to a PreferencesGroup header suffix."""
        section_keys = list(keys)
        btn_reset = attach_header_suffix_reset(
            group,
            lambda: self._on_section_reset_clicked(section_title, section_keys),
        )
        self.section_reset_entries.append((section_title, section_keys, btn_reset))
        return btn_reset

    def _on_section_reset_clicked(self, section_title: str, keys: list[str]) -> None:
        changes = self._collect_section_factory_changes(keys)
        if not changes:
            self.on_changed()
            return
        show_section_reset_dialog(
            self,
            section_title,
            changes,
            lambda: self._apply_section_factory_reset(section_title, keys),
        )

    def _collect_default_updates(self, keys) -> dict[str, str]:
        updates: dict[str, str] = {}
        for key in keys:
            if key in self.registered_switches:
                _row, fallback = self.registered_switches[key]
                def_bool = as_bool(
                    self._get_default_value(key, fallback),
                    default=bool(fallback),
                )
                updates[key] = bool_to_str(def_bool)
            elif key in self.registered_combos:
                _row, _options, _label_map, fallback = self.registered_combos[key]
                updates[key] = str(self._get_default_value(key, fallback))
        return updates

    def _apply_window_updates_to_ui(self, updates: dict[str, str]) -> None:
        self._loading_settings = True
        try:
            for key, str_val in updates.items():
                self.pending_window_values.pop(key, None)
                self.initial_values[key] = str_val
                self.current_values[key] = str_val
                if key in self.registered_switches:
                    row, fallback = self.registered_switches[key]
                    row.set_active(as_bool(str_val, default=bool(fallback)))
                elif key in self.registered_combos:
                    row, options, _label_map, _fallback = self.registered_combos[key]
                    if str_val in options:
                        row.set_selected(options.index(str_val))
        finally:
            self._loading_settings = False

    def _apply_section_factory_reset(self, section_title: str, keys: list[str]) -> bool:
        """Restores factory defaults for a single section and persists immediately."""
        updates = self._collect_default_updates(keys)
        if updates and not save_dbus_values(self.dbus_handler, updates):
            self.add_toast(
                Adw.Toast.new(_("Failed to save settings. Please check if Fcitx5 is running."))
            )
            return False

        self._apply_window_updates_to_ui(updates)
        self.on_changed()
        self.add_toast(Adw.Toast.new(_("Restored defaults for {}").format(section_title)))
        return True

    def _update_offline_banner(self) -> None:
        if hasattr(self, "offline_banner"):
            self.offline_banner.set_revealed(getattr(self.dbus_handler, "iface", None) is None)

    def _on_retry_dbus(self, _banner=None) -> None:
        self.dbus_handler = LotusDBusHandler()
        for page in self.subpages:
            if hasattr(page, "dbus"):
                page.dbus = self.dbus_handler
        self.reload_all()

    def _on_close_request(self, _window=None) -> bool:
        if self._force_close or not (self.pending_window_values or self.modified_pages):
            return False
        self._present_close_dialog()
        return True

    def _present_close_dialog(self) -> None:
        if self._close_dialog is not None:
            return
        heading = _("Unsaved Changes")
        body = _("You have unsaved settings. Save before closing?")
        has_errors = bool(self._page_errors)

        if hasattr(Adw, "AlertDialog"):
            dialog = Adw.AlertDialog(heading=heading, body=body)
        else:
            dialog = Adw.MessageDialog(transient_for=self, heading=heading, body=body)

        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("discard", _("Discard"))
        dialog.add_response("apply", _("Apply & Close"))
        dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_response_appearance("apply", Adw.ResponseAppearance.SUGGESTED)
        dialog.set_response_enabled("apply", not has_errors)
        dialog.set_default_response("cancel" if has_errors else "apply")
        dialog.set_close_response("cancel")

        def on_response(_dlg, response):
            self._close_dialog = None
            if response == "discard":
                self._force_close = True
                self.close()
            elif response == "apply":
                self._on_apply_clicked()
                if not self.pending_window_values and not self.modified_pages:
                    self._force_close = True
                    self.close()

        dialog.connect("response", on_response)
        self._close_dialog = dialog
        if hasattr(Adw, "AlertDialog"):
            dialog.present(self)
        else:
            dialog.present()

    def reload_all(self) -> None:
        """Reloads configuration from D-Bus across the window and all subpages."""
        self.config_data = self.dbus_handler.get_config() or {}
        self._parse_metadata()
        self.initial_values = dict(self.config_data.get("values", {}))
        self.current_values = dict(self.initial_values)
        self.pending_window_values.clear()
        self.modified_pages.clear()
        self._page_apply_callbacks.clear()
        self._page_cancel_callbacks.clear()
        self._page_errors.clear()
        self._reload_window_values()
        for page in self.subpages:
            load_fn = get_explicit_method(page, "load_data")
            if load_fn is not None:
                try:
                    load_fn()
                except Exception as e:
                    print(f"Error reloading page {page}: {e}")
        self._update_offline_banner()
        self.on_changed()

    def _on_apply_clicked(self, _button=None, show_toast: bool = True) -> bool:
        if self._page_errors:
            first_error = next(iter(self._page_errors.values()), _("Invalid settings."))
            self.add_toast(Adw.Toast.new(first_error))
            return False

        if self.pending_window_values:
            payload = {
                k: bool_to_str(v) if isinstance(v, bool) else str(v)
                for k, v in self.pending_window_values.items()
            }
            if not save_dbus_values(self.dbus_handler, payload):
                self.add_toast(
                    Adw.Toast.new(_("Failed to save settings. Please check if Fcitx5 is running."))
                )
                return False
            self.initial_values.update(payload)
            self.current_values.update(payload)
            self.pending_window_values.clear()

        failed_pages = []
        for page_id, cb in list(self._page_apply_callbacks.items()):
            try:
                result = cb()
                if result is False:
                    failed_pages.append(page_id)
                else:
                    self.modified_pages.discard(page_id)
                    self._page_apply_callbacks.pop(page_id, None)
                    self._page_cancel_callbacks.pop(page_id, None)
            except Exception as e:
                print(f"Error applying changes for {page_id}: {e}")
                failed_pages.append(page_id)

        self.on_changed()
        if failed_pages:
            self.add_toast(
                Adw.Toast.new(_("Failed to save some settings. Please check if Fcitx5 is running."))
            )
            return False

        if show_toast:
            self.add_toast(Adw.Toast.new(_("Settings saved")), is_save_toast=True)
        return True

    def _on_cancel_clicked(self, _button=None):
        if self.pending_window_values:
            self.pending_window_values.clear()
            self.current_values = dict(self.initial_values)
            self._reload_window_values()

        callbacks = list(self._page_cancel_callbacks.items())
        self.modified_pages.clear()
        self._page_apply_callbacks.clear()
        self._page_cancel_callbacks.clear()

        for page_id, cb in callbacks:
            try:
                cb()
            except Exception as e:
                print(f"Error cancelling changes for {page_id}: {e}")

        self.on_changed()

    def _restore_defaults(self):
        all_keys = list(self.registered_switches) + list(self.registered_combos)
        updates = self._collect_default_updates(all_keys)
        if updates and not save_dbus_values(self.dbus_handler, updates):
            self.add_toast(
                Adw.Toast.new(_("Failed to save settings. Please check if Fcitx5 is running."))
            )
            return

        self._apply_window_updates_to_ui(updates)

        failed_pages = []
        for page in self.subpages:
            restore_fn = get_explicit_method(page, "restore_defaults")
            if restore_fn is None:
                continue
            try:
                restore_fn()
                apply_fn = get_explicit_method(page, "apply_changes")
                has_err_fn = get_explicit_method(page, "has_validation_errors")
                if apply_fn is not None and not (has_err_fn is not None and has_err_fn()):
                    if apply_fn() is False:
                        failed_pages.append(page)
            except Exception as e:
                print(f"Error restoring defaults for {page}: {e}")
                failed_pages.append(page)

        self.on_changed()
        if failed_pages:
            self.add_toast(
                Adw.Toast.new(_("Failed to save some settings. Please check if Fcitx5 is running."))
            )
        else:
            self.add_toast(Adw.Toast.new(_("Restored all defaults")))

    def _reload_window_values(self):
        self._loading_settings = True
        try:
            for key, (row, fallback) in self.registered_switches.items():
                raw = self._get_effective_initial(key, fallback)
                row.set_active(as_bool(raw, default=fallback))
            for key, (row, options, _label_map, fallback) in self.registered_combos.items():
                cur = str(self._get_effective_initial(key, fallback))
                if cur in options:
                    row.set_selected(options.index(cur))
        finally:
            self._loading_settings = False

    def _build_floating_action_bar(self) -> Gtk.Revealer:
        self.action_revealer = Gtk.Revealer()
        self.action_revealer.set_transition_type(Gtk.RevealerTransitionType.SLIDE_UP)
        self.action_revealer.set_transition_duration(_REVEAL_ANIMATION_MS)
        self.action_revealer.set_halign(Gtk.Align.CENTER)
        self.action_revealer.set_valign(Gtk.Align.END)
        self.action_revealer.set_margin_bottom(_FLOATING_BAR_DEFAULT_MARGIN)
        self.action_revealer.set_reveal_child(False)
        self.action_revealer.set_can_target(False)
        self.action_revealer.set_visible(False)

        pill_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        pill_bar.add_css_class("floating-action-bar")

        self.btn_cancel = Gtk.Button(label=_("Cancel"))
        self.btn_cancel.add_css_class("flat")
        self.btn_cancel.add_css_class("floating-btn")
        self.btn_cancel.add_css_class("cancel-btn")
        self.btn_cancel.set_sensitive(False)
        self.btn_cancel.connect("clicked", self._on_cancel_clicked)
        pill_bar.append(self.btn_cancel)

        self.btn_apply = Gtk.Button(label=_("Apply"))
        self.btn_apply.add_css_class("floating-btn")
        self.btn_apply.add_css_class("suggested-action")
        self.btn_apply.set_sensitive(False)
        self.btn_apply.connect("clicked", self._on_apply_clicked)
        pill_bar.append(self.btn_apply)

        self.action_revealer.set_child(pill_bar)
        return self.action_revealer

    def _setup_ui(self):
        self.split_view = Adw.NavigationSplitView()
        self.split_view.set_min_sidebar_width(_MIN_SIDEBAR_WIDTH)
        self.split_view.set_max_sidebar_width(_MAX_SIDEBAR_WIDTH)

        self.view_stack = Adw.ViewStack()
        self.view_stack.add_css_class("settings-view-stack")
        self.view_stack.set_vexpand(True)
        self.view_stack.connect("notify::visible-child", self._on_visible_child_changed)

        # Sidebar pane
        sidebar_toolbar = Adw.ToolbarView()
        sidebar_toolbar.add_css_class("sidebar-pane")

        sidebar_header = Adw.HeaderBar()
        sidebar_header.set_show_end_title_buttons(False)
        sidebar_header.set_title_widget(Adw.WindowTitle(title=_("Lotus Settings")))
        sidebar_toolbar.add_top_bar(sidebar_header)

        sidebar_scroll = Gtk.ScrolledWindow()
        sidebar_scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sidebar_scroll.set_vexpand(True)

        self.sidebar = Gtk.ListBox()
        self.sidebar.add_css_class("navigation-sidebar")
        self.sidebar.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.sidebar.connect("row-selected", self._on_sidebar_row_selected)
        sidebar_scroll.set_child(self.sidebar)

        about_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        about_box.set_margin_start(10)
        about_box.set_margin_end(10)
        about_box.set_margin_top(6)
        about_box.set_margin_bottom(10)

        self.btn_about = Gtk.Button()
        self.btn_about.add_css_class("flat")
        self.btn_about.set_hexpand(True)
        self.btn_about.set_tooltip_text(_("About"))
        about_content = Adw.ButtonContent()
        about_content.set_icon_name("help-about-symbolic")
        about_content.set_label(_("About"))
        self.btn_about.set_child(about_content)
        self.btn_about.connect("clicked", self._on_about_clicked)
        about_box.append(self.btn_about)

        sidebar_content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        sidebar_content.set_vexpand(True)
        sidebar_content.append(sidebar_scroll)
        sidebar_content.append(about_box)
        sidebar_toolbar.set_content(sidebar_content)

        sidebar_page = Adw.NavigationPage(child=sidebar_toolbar, title=_("Lotus Settings"))
        self.split_view.set_sidebar(sidebar_page)

        # Content pane
        content_toolbar = Adw.ToolbarView()

        content_header = Adw.HeaderBar()
        content_header.set_show_start_title_buttons(False)
        self.content_title = Adw.WindowTitle(title=_("General"))
        content_header.set_title_widget(self.content_title)
        content_toolbar.add_top_bar(content_header)

        self.offline_banner = Adw.Banner(
            title=_("Fcitx5 is not running. Settings cannot be loaded or saved."),
            button_label=_("Retry"),
        )
        self.offline_banner.set_revealed(getattr(self.dbus_handler, "iface", None) is None)
        self.offline_banner.connect("button-clicked", self._on_retry_dbus)
        content_toolbar.add_top_bar(self.offline_banner)

        content_overlay = Gtk.Overlay()
        content_overlay.set_vexpand(True)
        content_overlay.set_hexpand(True)
        content_overlay.set_child(self.view_stack)
        content_overlay.add_overlay(self._build_floating_action_bar())

        self.toast_overlay = Adw.ToastOverlay()
        self.toast_overlay.set_vexpand(True)
        self.toast_overlay.set_hexpand(True)
        self.toast_overlay.set_child(content_overlay)
        content_toolbar.set_content(self.toast_overlay)

        content_page = Adw.NavigationPage(child=content_toolbar, title=_("Settings"))
        self.split_view.set_content(content_page)
        self.set_content(self.split_view)

        # Consolidated Tabs
        self._setup_general_page()
        self._setup_typing_page()
        self._setup_subpages()

        first_row = self._sidebar_rows.get("general") or self.sidebar.get_row_at_index(0)
        if first_row is not None:
            self.sidebar.select_row(first_row)

    @staticmethod
    def _resolve_icon_name(icon_names: str | tuple[str, ...]) -> str:
        """Picks the first icon name available in the active icon theme."""
        if isinstance(icon_names, str):
            return icon_names
        display = Gdk.Display.get_default()
        if display is not None:
            icon_theme = Gtk.IconTheme.get_for_display(display)
            for name in icon_names:
                if icon_theme.has_icon(name):
                    return name
        return icon_names[0]

    def _add_sidebar_page(
        self,
        page_widget: Gtk.Widget,
        page_id: str,
        title: str,
        icon_name: str | tuple[str, ...],
    ) -> None:
        resolved_icon = self._resolve_icon_name(icon_name)
        if hasattr(self.view_stack, "add_titled_with_icon"):
            self.view_stack.add_titled_with_icon(page_widget, page_id, title, resolved_icon)
        else:
            self.view_stack.add_titled(page_widget, page_id, title)

        row = Gtk.ListBoxRow()
        row._page_id = page_id
        row._page_title = title

        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        box.set_margin_top(12)
        box.set_margin_bottom(12)
        box.set_margin_start(10)
        box.set_margin_end(10)

        icon = Gtk.Image.new_from_icon_name(resolved_icon)
        icon.set_pixel_size(18)
        icon.set_valign(Gtk.Align.CENTER)
        label = Gtk.Label(label=title, xalign=0.0)
        label.set_valign(Gtk.Align.CENTER)
        label.set_hexpand(True)

        box.append(icon)
        box.append(label)
        row.set_child(box)

        self.sidebar.append(row)
        self._sidebar_rows[page_id] = row

    def _on_sidebar_row_selected(self, _listbox, row) -> None:
        if row is None:
            return
        page_id = getattr(row, "_page_id", None)
        if not page_id:
            return
        self.view_stack.set_visible_child_name(page_id)
        title = getattr(row, "_page_title", None)
        if title:
            self.content_title.set_title(title)

    def _setup_subpages(self):
        from adw_ui.pages.dictionary import DictEditorPage
        from adw_ui.pages.extra import BackupPage
        from adw_ui.pages.keymap import KeymapEditorPage
        from adw_ui.pages.macros import MacroEditorPage
        from adw_ui.pages.mode_manager import ModeManagerPage
        from adw_ui.pages.shortcuts import ShortcutsPage

        subpage_specs = [
            (
                "shortcuts",
                _("Shortcuts"),
                (
                    "configure-shortcuts-symbolic",
                    "preferences-desktop-keyboard-shortcuts-symbolic",
                    "input-keyboard-symbolic",
                ),
                ShortcutsPage,
                True,
            ),
            (
                "applications",
                _("Applications"),
                "applications-system-symbolic",
                ModeManagerPage,
                True,
            ),
            ("macros", _("Macros"), "insert-text-symbolic", MacroEditorPage, True),
            (
                "dictionary",
                _("Dictionary"),
                "accessories-dictionary-symbolic",
                DictEditorPage,
                True,
            ),
            ("keymap", _("Keymap"), "input-keyboard-symbolic", KeymapEditorPage, True),
            ("backup", _("Backup"), "document-save-symbolic", BackupPage, False),
        ]

        for page_id, title, icon_name, page_cls, is_staged in subpage_specs:
            page = (
                page_cls(self.dbus_handler, on_modified_changed=self.set_page_modified)
                if is_staged
                else page_cls(self.dbus_handler)
            )
            self.subpages.append(page)
            self._add_sidebar_page(page, page_id, title, icon_name)

        self.sc_page = self.subpages[0]

    def _on_about_clicked(self, _button):
        show_about_dialog(self, self.dbus_handler)

    def _on_visible_child_changed(self, stack, _pspec):
        child = stack.get_visible_child()
        if child:
            page = stack.get_page(child)
            if page:
                self.content_title.set_title(page.get_title())
                row = self._sidebar_rows.get(page.get_name())
                if row is not None and self.sidebar.get_selected_row() is not row:
                    self.sidebar.select_row(row)

    def _add_combo_row(
        self,
        group: Adw.PreferencesGroup,
        key: str,
        title: str,
        fallback_options: list[tuple[str, str]],
        default_val: str,
        translate_labels: bool = True,
    ) -> Adw.ComboRow:
        options = self._get_enum_options(
            key,
            fallback_options,
            translate_labels=translate_labels,
        )
        option_keys = [raw for raw, _ in options]
        option_labels = [label for _, label in options]
        label_map = {raw: label for raw, label in options}

        row = Adw.ComboRow()
        row.set_use_markup(False)
        row.set_title(title)
        row.set_model(Gtk.StringList.new(option_labels))
        helper = HELPERS.get(key)
        if helper:
            row.set_tooltip_text(_(helper))

        cur_val = str(self._get_value(key, default_val))
        if cur_val in option_keys:
            row.set_selected(option_keys.index(cur_val))
        elif default_val in option_keys:
            row.set_selected(option_keys.index(default_val))

        def on_selected(combo_row, _pspec):
            idx = combo_row.get_selected()
            if 0 <= idx < len(option_keys):
                self._on_setting_changed(key, option_keys[idx], default_fallback=default_val)

        row.connect("notify::selected", on_selected)
        group.add(row)
        self.registered_combos[key] = (row, option_keys, label_map, default_val)
        return row

    def _add_switch_row(
        self,
        group: Adw.PreferencesGroup,
        key: str,
        title: str,
        default_val: bool = False,
    ) -> Adw.SwitchRow:
        row = Adw.SwitchRow()
        row.set_use_markup(False)
        row.set_title(title)
        helper = HELPERS.get(key)
        if helper:
            row.set_tooltip_text(_(helper))

        val = as_bool(self._get_value(key, default_val), default=default_val)
        row.set_active(val)
        row.connect(
            "notify::active",
            lambda r, _pspec: self._on_setting_changed(
                key, r.get_active(), default_fallback=default_val
            ),
        )
        group.add(row)
        self.registered_switches[key] = (row, default_val)
        return row

    def _setup_general_page(self):
        page = Adw.PreferencesPage()

        # Group: Input
        group_input = Adw.PreferencesGroup()
        group_input.set_title(_("Input"))
        self._add_combo_row(
            group_input,
            "InputMethod",
            _("Input Method"),
            FALLBACK_INPUT_METHODS,
            FALLBACK_DEFAULTS["InputMethod"],
            translate_labels=False,
        )
        self._add_combo_row(
            group_input,
            "Mode",
            _("Input Mode"),
            FALLBACK_MODES,
            FALLBACK_DEFAULTS["Mode"],
        )
        self._add_combo_row(
            group_input,
            "OutputCharset",
            _("Output Charset"),
            FALLBACK_CHARSETS,
            FALLBACK_DEFAULTS["OutputCharset"],
            translate_labels=False,
        )
        self._attach_section_reset(
            group_input, _("Input"), ["InputMethod", "Mode", "OutputCharset"]
        )
        page.add(group_input)

        # Group: Quick Keys
        group_quick = Adw.PreferencesGroup()
        group_quick.set_title(_("Quick Keys"))
        self._add_combo_row(
            group_quick,
            "W2U",
            _("Type w as ư"),
            FALLBACK_TRIGGERS,
            FALLBACK_DEFAULTS["W2U"],
        )
        self._add_combo_row(
            group_quick,
            "BracketTransform",
            _("Bracket Keys"),
            FALLBACK_TRIGGERS,
            FALLBACK_DEFAULTS["BracketTransform"],
        )
        self._attach_section_reset(group_quick, _("Quick Keys"), ["W2U", "BracketTransform"])
        page.add(group_quick)

        # Group: Tray Icon
        group_tray = Adw.PreferencesGroup()
        group_tray.set_title(_("Tray Icon"))
        self._add_switch_row(
            group_tray,
            "UseLotusIcons",
            _("Use Lotus Icons"),
            as_bool(FALLBACK_DEFAULTS["UseLotusIcons"]),
        )
        self._add_combo_row(
            group_tray,
            "IconTheme",
            _("Icon Theme"),
            FALLBACK_ICON_THEMES,
            FALLBACK_DEFAULTS["IconTheme"],
        )
        self._attach_section_reset(group_tray, _("Tray Icon"), ["UseLotusIcons", "IconTheme"])
        page.add(group_tray)

        # Group: Backend
        group_backend = Adw.PreferencesGroup()
        group_backend.set_title(_("Backend"))
        self._add_switch_row(
            group_backend,
            "PreferVirtualKeyboard",
            _("System Keyboard Injection"),
            as_bool(FALLBACK_DEFAULTS["PreferVirtualKeyboard"]),
        )
        self._attach_section_reset(group_backend, _("Backend"), ["PreferVirtualKeyboard"])
        page.add(group_backend)

        self._add_sidebar_page(page, "general", _("General"), "preferences-system-symbolic")

    def _setup_typing_page(self):
        page = Adw.PreferencesPage()

        # Group: Spelling
        group_spell = Adw.PreferencesGroup()
        group_spell.set_title(_("Spelling"))
        self._add_switch_row(
            group_spell,
            "SpellCheck",
            _("Spell Check"),
            as_bool(FALLBACK_DEFAULTS["SpellCheck"]),
        )
        self._add_switch_row(
            group_spell,
            "AutoNonVnRestore",
            _("Auto Restore"),
            as_bool(FALLBACK_DEFAULTS["AutoNonVnRestore"]),
        )
        self._add_switch_row(
            group_spell,
            "DdFreeStyle",
            _("Free dd to đ"),
            as_bool(FALLBACK_DEFAULTS["DdFreeStyle"]),
        )
        self._attach_section_reset(
            group_spell,
            _("Spelling"),
            ["SpellCheck", "AutoNonVnRestore", "DdFreeStyle"],
        )
        page.add(group_spell)

        # Group: Tone & Punctuation
        group_tone = Adw.PreferencesGroup()
        group_tone.set_title(GLib.markup_escape_text(_("Tone & Punctuation"), -1))
        self._add_switch_row(
            group_tone,
            "ModernStyle",
            _("Modern Tone Style"),
            as_bool(FALLBACK_DEFAULTS["ModernStyle"]),
        )
        self._add_switch_row(
            group_tone,
            "FreeMarking",
            _("Free Tone Marking"),
            as_bool(FALLBACK_DEFAULTS["FreeMarking"]),
        )
        self._add_switch_row(
            group_tone,
            "AutoCapitalizeAfterPunctuation",
            _("Auto Capitalize"),
            as_bool(FALLBACK_DEFAULTS["AutoCapitalizeAfterPunctuation"]),
        )
        self._add_switch_row(
            group_tone,
            "DoubleSpaceToPeriod",
            _("Double Space Period"),
            as_bool(FALLBACK_DEFAULTS["DoubleSpaceToPeriod"]),
        )
        self._add_switch_row(
            group_tone,
            "DoubleHyphenToEmDash",
            _("Double Hyphen Dash"),
            as_bool(FALLBACK_DEFAULTS["DoubleHyphenToEmDash"]),
        )
        self._attach_section_reset(
            group_tone,
            _("Tone & Punctuation"),
            [
                "ModernStyle",
                "FreeMarking",
                "AutoCapitalizeAfterPunctuation",
                "DoubleSpaceToPeriod",
                "DoubleHyphenToEmDash",
            ],
        )
        page.add(group_tone)

        self._add_sidebar_page(page, "typing", _("Typing"), "accessories-character-map-symbolic")


LotusSettingsWindow = LotusPreferencesWindow
