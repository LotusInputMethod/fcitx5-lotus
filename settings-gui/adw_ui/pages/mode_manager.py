# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mode Manager Page for per-application input mode and timing configuration (Adwaita)."""

import copy
import os

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, Gdk, Gtk, Pango
from i18n import _

from adw_ui.app_detector import DesktopAppIndex, detect_running_apps
from adw_ui.pages.base import StagedPage
from adw_ui.utils import (
    clear_children,
    make_empty_row,
    make_icon_button,
    remove_rows_from_group,
    save_dbus_values,
    set_entry_error,
)
from adw_ui.widgets.section_header import attach_header_suffix_reset, build_list_section_header

# Mode constants as defined in C++ LotusEngine
MODE_OFF = 0
MODE_SMOOTH = 1
MODE_SUPER_SMOOTH = 3
MODE_SURROUNDING = 4
MODE_PREEDIT = 5
MODE_EMOJI = 6
MODE_DEFAULT = -1  # UI special value for "Use Global Default"

MODE_INFO = {
    MODE_DEFAULT: {"title": "Default Typing", "icon": "preferences-system-symbolic"},
    MODE_OFF: {"title": "OFF", "icon": "input-keyboard-symbolic"},
    MODE_SMOOTH: {"title": "Uinput (Smooth)", "icon": "input-keyboard-symbolic"},
    MODE_SUPER_SMOOTH: {"title": "Uinput (Super Smooth)", "icon": "input-keyboard-symbolic"},
    MODE_SURROUNDING: {"title": "Surrounding Text", "icon": "document-edit-symbolic"},
    MODE_PREEDIT: {"title": "Preedit", "icon": "document-edit-symbolic"},
    MODE_EMOJI: {"title": "Emoji Picker", "icon": "face-smile-symbolic"},
}

# Modes offered as selectable per-app modes, in display order (matching upstream/dev).
SELECTABLE_MODES = [
    MODE_SMOOTH,
    MODE_SUPER_SMOOTH,
    MODE_SURROUNDING,
    MODE_PREEDIT,
    MODE_EMOJI,
    MODE_OFF,
]

# Global default delays (config key -> factory default in ms).
DEFAULT_DELAY_KEYS = {
    "DefaultCommitInterval": 20,
    "DefaultBackspaceInterval": 5,
    "DefaultPostDelay": 10,
}

_MAX_DELAY_MS = 1000
_APP_ICON_SIZE = 28
_SEARCH_VISIBLE_THRESHOLD = 3
_RESET_PREVIEW_COUNT = 4
_DETECT_DIALOG_WIDTH = 460
_DETECT_DIALOG_HEIGHT = 520


class ModeManagerPage(StagedPage):
    """PreferencesPage for configuring default timings and per-application rules."""

    PAGE_ID = "applications"

    def __init__(self, dbus_handler: LotusDBusHandler, on_modified_changed=None, **kwargs):
        super().__init__(dbus_handler, on_modified_changed=on_modified_changed, **kwargs)

        self.app_rules = {}
        self._initial_app_rules = {}
        self._current_delays = dict(DEFAULT_DELAY_KEYS)
        self._initial_delays = dict(DEFAULT_DELAY_KEYS)

        self._delay_rows = {}
        self._app_rows = []
        self._row_widgets = {}
        self._desktop_index = DesktopAppIndex()
        self._detected_apps_cache = []
        self._search_text = ""
        self.detect_dialog = None
        self.detect_listbox = None
        self.detect_search_entry = None

        self._scan_desktop_files()
        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        # Group 1: Default Timing
        self.group_timing = Adw.PreferencesGroup()
        self.group_timing.set_title(_("Default Timing"))
        self.btn_reset_timing = attach_header_suffix_reset(
            self.group_timing, self._reset_timing_section
        )

        timing_defs = [
            (
                "DefaultCommitInterval",
                _("Commit Interval (ms)"),
                _("Cooldown between committed replacements when an app rule uses 0"),
            ),
            (
                "DefaultBackspaceInterval",
                _("Backspace Interval (ms)"),
                _("Delay between simulated backspaces when an app rule uses 0"),
            ),
            (
                "DefaultPostDelay",
                _("Post Delay (ms)"),
                _("Delay before replaying the final trigger key when an app rule uses 0"),
            ),
        ]
        for key, title, tooltip in timing_defs:
            row = Adw.SpinRow.new_with_range(0, _MAX_DELAY_MS, 1)
            row.set_use_markup(False)
            row.set_title(title)
            row.set_tooltip_text(tooltip)
            row.set_value(DEFAULT_DELAY_KEYS[key])
            row.connect(
                "notify::value",
                lambda r, _pspec, k=key: self._on_default_delay_changed(k, int(r.get_value())),
            )
            self._delay_rows[key] = row
            self.group_timing.add(row)

        self.add(self.group_timing)

        # Group 2: Application Rules
        self.group_apps = Adw.PreferencesGroup()
        self.group_apps.set_title(_("Application Rules"))

        self.btn_detect = make_icon_button(
            "find-location-symbolic",
            _("Detect Running App"),
            on_clicked=self._on_detect_running_clicked,
        )
        controls = build_list_section_header(
            self.group_apps,
            on_reset=self._reset_rules_section,
            on_add=self._on_add_rule_clicked,
            add_tooltip=_("Add Rule"),
            on_search_changed=self._on_search_changed,
            pre_add_widgets=[self.btn_detect],
            search_hidden=True,
        )
        self.btn_reset_rules = controls.btn_reset
        self.search_entry = controls.search_entry
        self.add(self.group_apps)

    def _on_detect_running_clicked(self, _button=None):
        dialog = Adw.Dialog()
        dialog.set_title(_("Running Apps"))
        dialog.set_content_width(_DETECT_DIALOG_WIDTH)
        dialog.set_content_height(_DETECT_DIALOG_HEIGHT)

        toolbar_view = Adw.ToolbarView()

        header_bar = Adw.HeaderBar()
        header_bar.add_css_class("flat")
        btn_refresh = make_icon_button(
            "view-refresh-symbolic",
            _("Refresh"),
            on_clicked=lambda _b: self._refresh_detect_dialog(),
        )
        header_bar.pack_start(btn_refresh)
        toolbar_view.add_top_bar(header_bar)

        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        content_box.set_margin_top(6)
        content_box.set_margin_bottom(16)
        content_box.set_margin_start(16)
        content_box.set_margin_end(16)

        search_entry = Gtk.SearchEntry()
        search_entry.set_placeholder_text(_("Search running apps…"))
        search_entry.set_hexpand(True)
        search_entry.connect("search-changed", lambda e: self._render_detect_rows(e.get_text()))
        content_box.append(search_entry)

        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scrolled.set_vexpand(True)

        listbox = Gtk.ListBox()
        listbox.set_selection_mode(Gtk.SelectionMode.NONE)
        listbox.add_css_class("boxed-list")
        scrolled.set_child(listbox)
        content_box.append(scrolled)

        toolbar_view.set_content(content_box)
        dialog.set_child(toolbar_view)

        self.detect_dialog = dialog
        self.detect_listbox = listbox
        self.detect_search_entry = search_entry

        self._refresh_detect_dialog()
        dialog.present(self.get_root())

    def _refresh_detect_dialog(self):
        self._detected_apps_cache = self._detect_running_apps()
        filter_text = (
            self.detect_search_entry.get_text() if self.detect_search_entry is not None else ""
        )
        self._render_detect_rows(filter_text)

    def _render_detect_rows(self, filter_text: str = ""):
        if self.detect_listbox is None:
            return
        clear_children(self.detect_listbox)

        query = filter_text.strip().lower()
        apps = [
            app
            for app in self._detected_apps_cache
            if not query
            or query in app["app_id"].lower()
            or query in (app.get("name") or "").lower()
        ]

        if not apps:
            self.detect_listbox.append(
                make_empty_row(_("No Matching Apps") if query else _("No Apps Found"))
            )
            return

        for app_info in apps:
            app_id = app_info["app_id"]
            display_name = app_info.get("name") or app_id
            row = Adw.ActionRow()
            row.set_use_markup(False)
            row.set_title(display_name)
            row.set_title_lines(1)
            if display_name.lower() != app_id.lower():
                row.set_subtitle(app_id)
                row.set_subtitle_lines(1)
            row.add_prefix(self._create_app_prefix_widget(app_id, display_name))

            if app_id in self.app_rules:
                badge = Gtk.Label(label=_("Added"))
                badge.add_css_class("dim-label")
                badge.add_css_class("caption")
                badge.set_valign(Gtk.Align.CENTER)
                row.add_suffix(badge)
                row.set_activatable(False)
                row.set_sensitive(False)
            else:
                add_icon = Gtk.Image.new_from_icon_name("list-add-symbolic")
                add_icon.add_css_class("dim-label")
                add_icon.set_valign(Gtk.Align.CENTER)
                row.add_suffix(add_icon)
                row.set_activatable(True)
                row.connect(
                    "activated",
                    lambda _r, a=app_id: self._on_detected_app_activated(a),
                )

            self.detect_listbox.append(row)

    def _on_detected_app_activated(self, app_id: str):
        if self.detect_dialog is not None:
            self.detect_dialog.close()
        self._add_or_select_app(app_id)

    def load_data(self):
        """Loads default timings and per-app rules via D-Bus and resets baseline."""
        with self._updating_guard():
            config = self.dbus.get_config()
            values = config.get("values", {}) if config else {}
            for key, default_val in DEFAULT_DELAY_KEYS.items():
                try:
                    val = int(values.get(key, default_val))
                except (ValueError, TypeError):
                    val = default_val
                val = max(0, min(_MAX_DELAY_MS, val))
                self._current_delays[key] = val
                self._delay_rows[key].set_value(val)
            self._initial_delays = dict(self._current_delays)

            self.app_rules = {}
            try:
                data = self.dbus.get_sub_config_list("app_rules", "Rules")
                for item in data:
                    try:
                        app = str(item.get("App", "")).strip()
                        mode = int(item.get("Mode", MODE_SMOOTH))
                        commit = int(item.get("CommitInterval", 0) or 0)
                        backspace = int(item.get("BackspaceInterval", 0) or 0)
                        post = int(item.get("PostDelay", 0) or 0)
                    except (ValueError, TypeError, AttributeError):
                        continue
                    if app:
                        if mode in (2, 7):
                            mode = MODE_SMOOTH
                        elif mode not in MODE_INFO or mode == MODE_DEFAULT:
                            mode = MODE_OFF
                        self.app_rules[app] = {
                            "mode": mode,
                            "commit": max(0, min(_MAX_DELAY_MS, commit)),
                            "backspace": max(0, min(_MAX_DELAY_MS, backspace)),
                            "post": max(0, min(_MAX_DELAY_MS, post)),
                        }
            except Exception as e:
                print(f"Error loading app rules via DBus: {e}")

            self._initial_app_rules = copy.deepcopy(self.app_rules)
            self._refresh_app_rows()

        self._notify_modified()

    def is_modified(self) -> bool:
        """Returns True if staged timings or app rules differ from the baseline."""
        return (
            self._current_delays != self._initial_delays
            or self.app_rules != self._initial_app_rules
        )

    def is_modified_from_default(self) -> bool:
        """Returns True if current settings differ from factory defaults."""
        return len(self.app_rules) > 0 or self._current_delays != DEFAULT_DELAY_KEYS

    def apply_changes(self) -> bool:
        """Writes staged timings and app rules to D-Bus and resets baseline."""
        try:
            delay_updates = {
                k: str(v)
                for k, v in self._current_delays.items()
                if v != self._initial_delays.get(k)
            }
            if delay_updates and not save_dbus_values(self.dbus, delay_updates):
                return False

            data = [
                {
                    "App": app,
                    "Mode": str(rule.get("mode", MODE_SMOOTH)),
                    "CommitInterval": str(rule.get("commit", 0)),
                    "BackspaceInterval": str(rule.get("backspace", 0)),
                    "PostDelay": str(rule.get("post", 0)),
                }
                for app, rule in sorted(self.app_rules.items())
            ]

            if not self.dbus.set_sub_config_list("app_rules", "Rules", data):
                return False

            self._initial_delays = dict(self._current_delays)
            self._initial_app_rules = copy.deepcopy(self.app_rules)
            self._notify_modified()
            return True
        except Exception as e:
            print(f"Error saving app rules via DBus: {e}")
            return False

    def cancel_changes(self):
        """Discards staged changes and restores the baseline state."""
        with self._updating_guard():
            self._current_delays = dict(self._initial_delays)
            for key, row in self._delay_rows.items():
                row.set_value(self._current_delays[key])
            self.app_rules = copy.deepcopy(self._initial_app_rules)
            self._refresh_app_rows()
        self._notify_modified()

    def restore_defaults(self):
        """Resets default timings and clears per-app rules in staged memory."""
        with self._updating_guard():
            self._current_delays = dict(DEFAULT_DELAY_KEYS)
            for key, row in self._delay_rows.items():
                row.set_value(self._current_delays[key])
            self.app_rules = {}
            self._refresh_app_rows()
        self._notify_modified()

    def _timing_reset_changes(self) -> list[tuple[str, str, str]]:
        labels = {
            "DefaultCommitInterval": _("Commit Interval (ms)"),
            "DefaultBackspaceInterval": _("Backspace Interval (ms)"),
            "DefaultPostDelay": _("Post Delay (ms)"),
        }
        return [
            (labels[k], f"{self._current_delays[k]} ms", f"{default_val} ms")
            for k, default_val in DEFAULT_DELAY_KEYS.items()
            if self._current_delays.get(k) != default_val
        ]

    def _rules_reset_changes(self) -> list[tuple[str, str, str]]:
        if not self.app_rules:
            return []
        app_names = sorted(self.app_rules)
        preview = ", ".join(app_names[:_RESET_PREVIEW_COUNT])
        if len(app_names) > _RESET_PREVIEW_COUNT:
            preview += ", …"
        current_desc = _("{count} rules ({apps})").format(count=len(app_names), apps=preview)
        return [(_("Application Rules"), current_desc, _("None (0 rules)"))]

    def get_factory_reset_changes(self) -> list[tuple[str, str, str]]:
        """Returns factory-reset diff rows for global reset confirmation."""
        return self._timing_reset_changes() + self._rules_reset_changes()

    def _reset_timing_section(self, *_args):
        """Prompts confirmation then factory-resets and persists Default Timing."""
        changes = self._timing_reset_changes()

        def apply_reset() -> bool:
            updates = {k: str(v) for k, v in DEFAULT_DELAY_KEYS.items()}
            if not save_dbus_values(self.dbus, updates):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self._current_delays = dict(DEFAULT_DELAY_KEYS)
                for key, row in self._delay_rows.items():
                    row.set_value(DEFAULT_DELAY_KEYS[key])
            self._initial_delays = dict(DEFAULT_DELAY_KEYS)
            return True

        self._prompt_section_reset(_("Default Timing"), changes, apply_reset)

    def _reset_rules_section(self, *_args):
        """Prompts confirmation then factory-resets and persists Application Rules."""
        changes = self._rules_reset_changes()

        def apply_reset() -> bool:
            if not self.dbus.set_sub_config_list("app_rules", "Rules", []):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self.app_rules = {}
                self._refresh_app_rows()
            self._initial_app_rules = {}
            return True

        self._prompt_section_reset(_("Application Rules"), changes, apply_reset)

    def _update_section_reset_buttons(self):
        self.btn_reset_timing.set_visible(self._current_delays != DEFAULT_DELAY_KEYS)
        self.btn_reset_rules.set_visible(len(self.app_rules) > 0)

    def _on_default_delay_changed(self, key: str, value: int):
        if self._updating:
            return
        self._current_delays[key] = value
        self._notify_modified()

    def _on_search_changed(self, entry):
        self._search_text = entry.get_text().strip().lower()
        self._refresh_app_rows()

    def _update_search_visibility(self):
        self.search_entry.set_visible(
            len(self.app_rules) > _SEARCH_VISIBLE_THRESHOLD or bool(self._search_text)
        )

    def _refresh_app_rows(self, expand_app: str | None = None):
        remove_rows_from_group(self.group_apps, self._app_rows)
        self._row_widgets.clear()
        self._update_search_visibility()

        filtered_items = []
        for app_id, rule in sorted(self.app_rules.items(), key=lambda kv: kv[0].lower()):
            display_name = self._display_name(app_id)
            if self._search_text and (
                self._search_text not in app_id.lower()
                and self._search_text not in display_name.lower()
            ):
                continue
            filtered_items.append((app_id, rule))

        if not filtered_items:
            title = (
                _("No Matching Apps")
                if self.app_rules and self._search_text
                else _("No Application Rules")
            )
            empty_row = make_empty_row(title)
            self.group_apps.add(empty_row)
            self._app_rows.append(empty_row)
            return

        for app_id, rule in filtered_items:
            row = self._create_app_row(app_id, rule)
            if expand_app and app_id == expand_app:
                row.set_expanded(True)
            self.group_apps.add(row)
            self._app_rows.append(row)

    def _format_rule_subtitle(self, app_id: str, rule: dict) -> str:
        has_custom_timing = any(
            int(rule.get(f, 0) or 0) > 0 for f in ("commit", "backspace", "post")
        )
        display_name = self._display_name(app_id)
        parts = []
        if display_name.lower() != app_id.lower():
            parts.append(app_id)
        if has_custom_timing:
            parts.append(_("Custom Timing"))
        return " · ".join(parts)

    def _create_app_row(self, app_id: str, rule: dict) -> Adw.ExpanderRow:
        expander = Adw.ExpanderRow()
        expander.set_use_markup(False)
        app_name = self._display_name(app_id)
        expander.set_title(app_name)
        expander.set_subtitle(self._format_rule_subtitle(app_id, rule))

        expander.add_prefix(self._create_app_prefix_widget(app_id, app_name))

        cur_mode = rule.get("mode", MODE_SMOOTH)
        mode_badge = Gtk.Label(label=_(MODE_INFO.get(cur_mode, MODE_INFO[MODE_SMOOTH])["title"]))
        mode_badge.add_css_class("mode-pill")
        mode_badge.set_valign(Gtk.Align.CENTER)
        expander.add_suffix(mode_badge)

        # Mode ComboRow inside expander
        mode_ids = list(SELECTABLE_MODES)
        if cur_mode not in mode_ids and cur_mode in MODE_INFO:
            mode_ids.append(cur_mode)

        mode_row = Adw.ComboRow()
        mode_row.set_use_markup(False)
        mode_row.set_title(_("Input Mode"))
        mode_row.set_model(Gtk.StringList.new([_(MODE_INFO[m]["title"]) for m in mode_ids]))
        if cur_mode in mode_ids:
            mode_row.set_selected(mode_ids.index(cur_mode))
        mode_row.connect(
            "notify::selected",
            lambda r, _pspec, a=app_id, mids=mode_ids: self._on_app_mode_changed(
                a, mids[r.get_selected()]
            ),
        )
        expander.add_row(mode_row)

        # Delay overrides inside expander (0 = inherit default)
        delay_fields = [
            ("commit", _("Commit Interval (ms)")),
            ("backspace", _("Backspace Interval (ms)")),
            ("post", _("Post Delay (ms)")),
        ]
        for field, title in delay_fields:
            spin = Adw.SpinRow.new_with_range(0, _MAX_DELAY_MS, 1)
            spin.set_use_markup(False)
            spin.set_title(title)
            spin.set_tooltip_text(_("0 = inherit default"))
            spin.set_value(int(rule.get(field, 0) or 0))
            spin.connect(
                "notify::value",
                lambda r, _pspec, a=app_id, f=field: self._on_app_delay_changed(
                    a, f, int(r.get_value())
                ),
            )
            expander.add_row(spin)

        # Remove Rule action row tucked inside expander body
        remove_row = Adw.ActionRow()
        remove_row.set_use_markup(False)
        remove_row.set_title(_("Remove Rule"))

        btn_del = Gtk.Button()
        btn_del.add_css_class("destructive-action")
        btn_del.set_valign(Gtk.Align.CENTER)
        del_content = Adw.ButtonContent()
        del_content.set_icon_name("user-trash-symbolic")
        del_content.set_label(_("Remove"))
        btn_del.set_child(del_content)
        btn_del.set_tooltip_text(_("Remove application rule"))
        btn_del.connect("clicked", lambda _b, a=app_id: self._remove_app_rule(a))
        remove_row.add_suffix(btn_del)
        remove_row.set_activatable_widget(btn_del)
        expander.add_row(remove_row)

        self._row_widgets[app_id] = {
            "expander": expander,
            "mode_badge": mode_badge,
            "btn_del": btn_del,
        }
        return expander

    def _on_app_mode_changed(self, app_id: str, mode: int):
        if self._updating or app_id not in self.app_rules:
            return
        self.app_rules[app_id]["mode"] = mode
        self._sync_row_summary(app_id)
        self._notify_modified()

    def _on_app_delay_changed(self, app_id: str, field: str, value: int):
        if self._updating or app_id not in self.app_rules:
            return
        self.app_rules[app_id][field] = value
        self._sync_row_summary(app_id)
        self._notify_modified()

    def _sync_row_summary(self, app_id: str):
        widgets = self._row_widgets.get(app_id)
        rule = self.app_rules.get(app_id)
        if not widgets or not rule:
            return
        mode = rule.get("mode", MODE_SMOOTH)
        mode_title = _(MODE_INFO.get(mode, MODE_INFO[MODE_SMOOTH])["title"])
        widgets["mode_badge"].set_label(mode_title)
        widgets["expander"].set_subtitle(self._format_rule_subtitle(app_id, rule))

    def _remove_app_rule(self, app_id: str):
        if app_id in self.app_rules:
            del self.app_rules[app_id]
            self._refresh_app_rows()
            self._notify_modified()

    def _add_or_select_app(self, app_id: str):
        if app_id not in self.app_rules:
            self.app_rules[app_id] = {
                "mode": MODE_SMOOTH,
                "commit": 0,
                "backspace": 0,
                "post": 0,
            }
            self._notify_modified()
        self._refresh_app_rows(expand_app=app_id)

    def _on_add_rule_clicked(self, _button):
        dialog = Adw.AlertDialog(
            heading=_("Add Application"),
            body=_("Enter binary name or window class (e.g. discord, code):"),
        )

        entry = Gtk.Entry()
        entry.set_placeholder_text("app-name")
        entry.set_activates_default(True)
        dialog.set_extra_child(entry)

        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("add", _("Add"))
        dialog.set_response_appearance("add", Adw.ResponseAppearance.SUGGESTED)
        dialog.set_default_response("add")
        dialog.set_close_response("cancel")
        dialog.set_response_enabled("add", False)

        def on_entry_changed(ent):
            text = ent.get_text().strip()
            exists = text in self.app_rules
            set_entry_error(ent, _("Rule already exists") if exists else "")
            dialog.set_response_enabled("add", bool(text) and not exists)

        entry.connect("changed", on_entry_changed)

        def on_response(_dlg, response):
            if response == "add":
                val = entry.get_text().strip()
                if val:
                    self._add_or_select_app(val)

        dialog.connect("response", on_response)
        dialog.present(self.get_root())

    def _scan_desktop_files(self):
        self._desktop_index.scan()

    def _detect_running_apps(self) -> list[dict]:
        return detect_running_apps(self._desktop_index)

    def _display_name(self, app_id: str) -> str:
        return self._desktop_index.display_name(app_id)

    def _lookup_desktop_info(self, app_id: str) -> dict | None:
        return self._desktop_index.lookup(app_id)

    def _resolve_icon_name(self, app_id: str) -> str:
        return self._desktop_index.resolve_icon_name(app_id)

    def _create_app_prefix_widget(self, app_id: str, display_name: str) -> Gtk.Widget:
        icon_name = self._resolve_icon_name(app_id)
        if icon_name:
            if os.path.isabs(icon_name) and os.path.isfile(icon_name):
                img = Gtk.Image.new_from_file(icon_name)
                img.set_pixel_size(_APP_ICON_SIZE)
                img.set_valign(Gtk.Align.CENTER)
                return img

            display = Gdk.Display.get_default()
            has_icon = False
            if display is not None:
                theme = Gtk.IconTheme.get_for_display(display)
                has_icon = theme.has_icon(icon_name)
            elif icon_name in self._desktop_index.icon_cache.values():
                has_icon = True

            if has_icon:
                img = Gtk.Image.new_from_icon_name(icon_name)
                img.set_pixel_size(_APP_ICON_SIZE)
                img.set_valign(Gtk.Align.CENTER)
                return img

        letter_src = (display_name or app_id or "?").strip()
        letter = letter_src[0].upper() if letter_src else "?"
        avatar = Gtk.Label(label=letter)
        avatar.add_css_class("app-avatar")
        avatar.set_size_request(_APP_ICON_SIZE, _APP_ICON_SIZE)
        avatar.set_halign(Gtk.Align.CENTER)
        avatar.set_valign(Gtk.Align.CENTER)
        avatar.set_ellipsize(Pango.EllipsizeMode.NONE)
        return avatar
