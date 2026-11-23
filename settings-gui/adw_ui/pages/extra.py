# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Backup and About Pages for Lotus Settings (Adwaita)."""

import getpass
import json
import os
import platform
import subprocess
import threading
from datetime import datetime

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, GLib, Gtk
from i18n import N_, _

from adw_ui.utils import (
    build_file_filters,
    get_explicit_method,
    local_dict_path,
    present_open_file_dialog,
    present_save_file_dialog,
    read_dict_words,
    register_css,
    reload_dbus_config,
    show_toast_for,
    write_dict_words_atomic,
)

try:
    from version import __version__
except ImportError:
    __version__ = "dev version"


_REPO_URL = "https://github.com/LotusInputMethod/fcitx5-lotus"
_BUG_URL = f"{_REPO_URL}/issues/new?template=bug_report.yml"
_FEATURE_URL = f"{_REPO_URL}/issues/new?template=feature_request.yml"
_BLANK_ISSUE_URL = f"{_REPO_URL}/issues/new"
_CONTRIBUTORS_URL = f"{_REPO_URL}/graphs/contributors"

_ABOUT_DIALOG_WIDTH = 520
_ABOUT_DIALOG_HEIGHT = 580
_HERO_ICON_SIZE = 48
_LOG_TAIL_LINES = 200
_CMD_TIMEOUT_S = 5.0
_DIAGNOSE_TIMEOUT_S = 10.0

_ENV_KEYS = (
    "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_TYPE",
    "WAYLAND_DISPLAY",
    "DISPLAY",
    "QT_IM_MODULE",
    "GTK_IM_MODULE",
    "XMODIFIERS",
)

_DBUS_COMPONENTS = ("config", "rules", "macros", "keymaps")

_COMPONENTS = [
    (
        "config",
        N_("General & Typing"),
        N_("Main input method, typing rules, shortcuts, and appearance"),
    ),
    (
        "rules",
        N_("Application Rules"),
        N_("Per-application input modes and timing overrides"),
    ),
    (
        "macros",
        N_("Macros"),
        N_("Macro behavior and shorthand expansions"),
    ),
    (
        "keymaps",
        N_("Custom Keymap"),
        N_("Custom key-to-action mappings"),
    ),
    (
        "dictionary",
        N_("Custom Dictionary"),
        N_("Custom excluded dictionary words"),
    ),
]


def _open_uri(parent, url: str):
    if hasattr(Gtk, "UriLauncher"):
        Gtk.UriLauncher.new(url).launch(parent, None, None)
    else:
        Gtk.show_uri(parent, url, 0)


def _run_command_output(cmd: list[str], timeout: float = _CMD_TIMEOUT_S) -> str:
    """Runs a diagnostic command safely and returns combined output or a short status."""
    try:
        res = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        output = (res.stdout or "").strip()
        err = (res.stderr or "").strip()
        if output and err:
            return f"{output}\n{err}"
        return output or err or f"(exit {res.returncode}, no output)"
    except FileNotFoundError:
        return f"(command not found: {cmd[0]})"
    except subprocess.TimeoutExpired:
        return f"(timed out after {timeout:.0f}s)"
    except (OSError, subprocess.SubprocessError) as e:
        return f"(error: {e})"


def collect_debug_info(dbus_handler: LotusDBusHandler | None = None) -> str:
    """Collects system, config, uinput server log, and fcitx5-diagnose information."""
    user = getpass.getuser()
    sections = []

    sections.append(
        "\n".join(
            [
                "=== Fcitx5 Lotus Debug Information ===",
                f"Version: {__version__}",
                f"Timestamp: {datetime.now().isoformat()}",
                f"User: {user}",
            ]
        )
    )

    os_release = "(unavailable)"
    if os.path.exists("/etc/os-release"):
        try:
            with open("/etc/os-release", "r", encoding="utf-8", errors="replace") as f:
                os_release = f.read().strip()
        except OSError as e:
            os_release = f"(error reading /etc/os-release: {e})"

    env_lines = [f"{k}={os.environ.get(k, '')}" for k in _ENV_KEYS]
    sections.append(
        "\n".join(
            [
                "=== System & Environment ===",
                f"Kernel: {' '.join(platform.uname())}",
                "--- /etc/os-release ---",
                os_release,
                "--- Environment ---",
                *env_lines,
            ]
        )
    )

    config_text = "(D-Bus handler unavailable)"
    if dbus_handler is not None:
        try:
            cfg = dbus_handler.get_config()
            if cfg and isinstance(cfg.get("values"), dict):
                config_text = json.dumps(cfg["values"], indent=2, ensure_ascii=False)
            else:
                config_text = "(empty or Fcitx5 not running)"
        except Exception as e:
            config_text = f"(error fetching config: {e})"
    sections.append(f"=== Lotus Configuration ===\n{config_text}")

    journal_tag = _run_command_output(
        ["journalctl", "-t", "fcitx5-lotus-server", "--no-pager", "-n", str(_LOG_TAIL_LINES)],
        timeout=_CMD_TIMEOUT_S,
    )
    sections.append(
        f"=== Uinput Server Journal (journalctl -t fcitx5-lotus-server) ===\n{journal_tag}"
    )

    journal_unit = _run_command_output(
        [
            "journalctl",
            "-u",
            f"fcitx5-lotus-server@{user}.service",
            "--no-pager",
            "-n",
            str(_LOG_TAIL_LINES),
        ],
        timeout=_CMD_TIMEOUT_S,
    )
    sections.append(
        f"=== Uinput Server Unit Journal (fcitx5-lotus-server@{user}.service) ===\n{journal_unit}"
    )

    fallback_log_path = "/tmp/fcitx5-lotus-server.log"
    if os.path.exists(fallback_log_path):
        try:
            with open(fallback_log_path, "r", encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()[-_LOG_TAIL_LINES:]
            sections.append(
                f"=== {fallback_log_path} (last {len(lines)} lines) ===\n" + "\n".join(lines)
            )
        except OSError as e:
            sections.append(f"=== {fallback_log_path} ===\n(error reading file: {e})")

    diagnose_out = _run_command_output(["fcitx5-diagnose"], timeout=_DIAGNOSE_TIMEOUT_S)
    sections.append(f"=== fcitx5-diagnose ===\n{diagnose_out}")

    return "\n\n".join(sections) + "\n"


def export_debug_info(path: str, dbus_handler: LotusDBusHandler | None = None) -> bool:
    """Writes collected debug information to ``path`` and returns True on success."""
    try:
        report = collect_debug_info(dbus_handler=dbus_handler)
        parent_dir = os.path.dirname(os.path.abspath(path))
        if parent_dir:
            os.makedirs(parent_dir, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(report)
        return True
    except Exception as e:
        print(f"Failed to export debug info: {e}")
        return False


def show_about_dialog(parent=None, dbus_handler: LotusDBusHandler | None = None):
    """Presents a rich Libadwaita Adw.Dialog hosting AboutPage."""
    if dbus_handler is None and parent is not None and hasattr(parent, "dbus_handler"):
        dbus_handler = parent.dbus_handler

    dialog = Adw.Dialog()
    dialog.set_title(_("About Fcitx5 Lotus"))
    dialog.set_content_width(_ABOUT_DIALOG_WIDTH)
    dialog.set_content_height(_ABOUT_DIALOG_HEIGHT)

    toolbar_view = Adw.ToolbarView()
    header_bar = Adw.HeaderBar()
    header_bar.add_css_class("flat")
    toolbar_view.add_top_bar(header_bar)

    about_page = AboutPage(dbus_handler=dbus_handler)
    toolbar_view.set_content(about_page)

    toast_overlay = Adw.ToastOverlay()
    toast_overlay.set_child(toolbar_view)
    dialog.set_child(toast_overlay)

    dialog.present(parent)
    return dialog


class BackupPage(Adw.PreferencesPage):
    """PreferencesPage for selective JSON export and import of Lotus configuration."""

    def __init__(self, dbus_handler: LotusDBusHandler, on_reload_requested=None, **kwargs):
        super().__init__(**kwargs)
        self.dbus = dbus_handler
        self._on_reload_requested = on_reload_requested
        self.switches = {}
        self._setup_ui()

    def _setup_ui(self):
        # Group 1: Components
        group_components = Adw.PreferencesGroup()
        group_components.set_title(_("Components"))

        for key, title, tooltip in _COMPONENTS:
            row = Adw.SwitchRow()
            row.set_use_markup(False)
            row.set_title(_(title))
            row.set_tooltip_text(_(tooltip))
            row.set_active(True)
            self.switches[key] = row
            group_components.add(row)

        self.add(group_components)

        # Group 2: Bundle
        group_bundle = Adw.PreferencesGroup()
        group_bundle.set_title(_("Bundle"))

        for title, tooltip, icon_name, callback in (
            (
                _("Export…"),
                _("Save selected components to a JSON backup file"),
                "document-save-as-symbolic",
                self._on_export_clicked,
            ),
            (
                _("Import…"),
                _("Restore selected components from a JSON backup file"),
                "document-open-symbolic",
                self._on_import_clicked,
            ),
        ):
            row = Adw.ActionRow()
            row.set_use_markup(False)
            row.set_title(title)
            row.set_tooltip_text(tooltip)
            row.set_activatable(True)
            row.connect("activated", lambda _r, cb=callback: cb(None))
            icon = Gtk.Image.new_from_icon_name(icon_name)
            icon.set_valign(Gtk.Align.CENTER)
            row.add_suffix(icon)
            group_bundle.add(row)

        self.add(group_bundle)

    def _selected_components(self) -> dict[str, bool]:
        return {k: sw.get_active() for k, sw in self.switches.items()}

    def _can_export(self, selected: dict[str, bool]) -> bool:
        if not any(selected.values()):
            self._show_toast(_("Select at least one component to export"))
            return False
        has_dbus_selected = any(selected.get(k) for k in _DBUS_COMPONENTS)
        if has_dbus_selected and not getattr(self.dbus, "iface", None):
            self._show_toast(_("Fcitx5 is not running. Cannot export settings."))
            return False
        if (
            not has_dbus_selected
            and selected.get("dictionary")
            and not os.path.exists(local_dict_path())
        ):
            self._show_toast(_("No custom dictionary file to export."))
            return False
        return True

    def _on_export_clicked(self, _button):
        selected = self._selected_components()
        if not self._can_export(selected):
            return

        default_name = f"lotus-backup-{datetime.now().strftime('%Y%m%d-%H%M%S')}.json"
        present_save_file_dialog(
            self.get_root(),
            _("Export Backup"),
            build_file_filters(_("JSON Backup"), ("*.json",)),
            default_name,
            self._do_export,
        )

    def _do_export(self, path: str) -> bool:
        selected = self._selected_components()
        if not self._can_export(selected):
            return False

        try:
            backup = {
                "meta": {
                    "version": 2,
                    "timestamp": datetime.now().isoformat(),
                    "app": "fcitx5-lotus",
                    "components": [],
                }
            }

            if selected["config"]:
                config = self.dbus.get_config()
                backup["config"] = config.get("values", {}) if config else {}

            if selected["rules"]:
                backup["rules"] = self.dbus.get_sub_config_list("app_rules", "Rules")

            if selected["macros"]:
                backup["macros"] = self.dbus.get_sub_config_list("lotus-macro", "Macro")

            if selected["keymaps"]:
                backup["keymaps"] = self.dbus.get_sub_config_list("custom_keymap", "CustomKeymap")

            if selected["dictionary"]:
                dict_path = local_dict_path()
                if os.path.exists(dict_path):
                    backup["dictionary"] = read_dict_words(dict_path)

            backup["meta"]["components"] = [k for k, v in selected.items() if v and k in backup]

            with open(path, "w", encoding="utf-8") as f:
                json.dump(backup, f, indent=2, ensure_ascii=False)

            self._show_toast(_("Backup exported"))
            return True
        except Exception as e:
            self._show_toast(_("Export failed: {}").format(e))
            return False

    def _on_import_clicked(self, _button):
        selected = self._selected_components()
        if not any(selected.values()):
            self._show_toast(_("Select at least one component to import"))
            return

        present_open_file_dialog(
            self.get_root(),
            _("Import Backup"),
            build_file_filters(_("JSON Backup"), ("*.json",)),
            self._confirm_and_import,
        )

    def _confirm_and_import(self, path: str):
        selected = self._selected_components()
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as e:
            self._show_toast(_("Failed to read backup: {}").format(e))
            return

        if not isinstance(data, dict):
            self._show_toast(_("Invalid backup file"))
            return

        matched_labels = [
            _(title) for key, title, _tip in _COMPONENTS if selected.get(key) and key in data
        ]
        if not matched_labels:
            self._show_toast(_("No matching components found in backup"))
            return

        dialog = Adw.AlertDialog(
            heading=_("Restore Backup?"),
            body=_("Overwrite selected components: {}?").format(", ".join(matched_labels)),
        )
        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("restore", _("Restore"))
        dialog.set_response_appearance("restore", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_default_response("cancel")
        dialog.set_close_response("cancel")
        dialog.connect(
            "response",
            lambda _dlg, response: (
                self._do_import(path, data=data) if response == "restore" else None
            ),
        )
        dialog.present(self.get_root())

    def _do_import(self, path: str, data: dict | None = None) -> bool:
        selected = self._selected_components()
        try:
            if data is None:
                with open(path, "r", encoding="utf-8") as f:
                    data = json.load(f)

            if not isinstance(data, dict):
                self._show_toast(_("Invalid backup file"))
                return False

            failed = []
            dict_restored = False
            config_reloaded = False

            # Write local dictionary override before set_config() so LotusEngine::loadDictionary()
            # reads the newly restored file when config reloads.
            if selected.get("dictionary") and "dictionary" in data:
                dict_payload = data["dictionary"]
                if isinstance(dict_payload, (list, str)):
                    try:
                        write_dict_words_atomic(local_dict_path(), dict_payload)
                        dict_restored = True
                    except OSError as e:
                        print(f"Failed to restore dictionary: {e}")
                        failed.append(_("Custom Dictionary"))

            if selected.get("config") and "config" in data and isinstance(data["config"], dict):
                if self.dbus.set_config(data["config"]):
                    config_reloaded = True
                else:
                    failed.append(_("General & Typing"))

            if selected.get("rules") and "rules" in data and isinstance(data["rules"], list):
                if not self.dbus.set_sub_config_list("app_rules", "Rules", data["rules"]):
                    failed.append(_("Application Rules"))

            if selected.get("macros") and "macros" in data and isinstance(data["macros"], list):
                if not self.dbus.set_sub_config_list("lotus-macro", "Macro", data["macros"]):
                    failed.append(_("Macros"))

            if selected.get("keymaps") and "keymaps" in data and isinstance(data["keymaps"], list):
                if not self.dbus.set_sub_config_list(
                    "custom_keymap", "CustomKeymap", data["keymaps"]
                ):
                    failed.append(_("Custom Keymap"))

            if dict_restored and not config_reloaded:
                reload_dbus_config(self.dbus)

            if failed:
                self._show_toast(_("Failed to restore: {}").format(", ".join(failed)))
                return False

            self._trigger_reload()
            self._show_toast(_("Backup restored"))
            return True
        except Exception as e:
            self._show_toast(_("Import failed: {}").format(e))
            return False

    def _trigger_reload(self):
        if callable(self._on_reload_requested):
            self._on_reload_requested()
            return
        root = self.get_root()
        if root is None:
            return
        reload_fn = get_explicit_method(root, "reload_all")
        if reload_fn is not None:
            reload_fn()
            return
        if (
            hasattr(root, "dbus_handler")
            and hasattr(root, "initial_values")
            and get_explicit_method(root, "_reload_window_values") is not None
        ):
            cfg = root.dbus_handler.get_config()
            root.config_data = cfg
            root.initial_values = dict(cfg.get("values", {})) if cfg else {}
            root.current_values = dict(root.initial_values)
            if hasattr(root, "pending_window_values"):
                root.pending_window_values.clear()
            root._reload_window_values()
        if hasattr(root, "subpages"):
            for page in root.subpages:
                load_fn = get_explicit_method(page, "load_data")
                if load_fn is not None:
                    load_fn()
        on_changed = get_explicit_method(root, "on_changed")
        if on_changed is not None:
            on_changed()

    def _show_toast(self, text: str):
        show_toast_for(self, text)


_ABOUT_CSS = """
preferencespage.about-page > scrolledwindow > viewport > clamp > box {
    padding-bottom: 16px;
}
"""


class AboutPage(Adw.PreferencesPage):
    """PreferencesPage presenting app identity, issue templates, diagnostics, and licenses."""

    def __init__(self, dbus_handler: LotusDBusHandler | None = None, **kwargs):
        super().__init__(**kwargs)
        self.dbus = dbus_handler
        self.add_css_class("about-page")
        register_css("about", _ABOUT_CSS)
        self._setup_ui()

    def _add_link_row(
        self, group: Adw.PreferencesGroup, title: str, url: str, tooltip: str | None = None
    ) -> None:
        row = Adw.ActionRow()
        row.set_use_markup(False)
        row.set_title(title)
        row.set_tooltip_text(tooltip if tooltip is not None else url)
        row.set_activatable(True)
        row.connect("activated", lambda _r, u=url: _open_uri(self.get_root(), u))
        icon = Gtk.Image.new_from_icon_name("adw-external-link-symbolic")
        icon.set_valign(Gtk.Align.CENTER)
        row.add_suffix(icon)
        group.add(row)

    def _setup_ui(self):
        # Group 1: App Identity Hero
        group_app = Adw.PreferencesGroup()

        row_app = Adw.ActionRow()
        row_app.set_use_markup(False)
        row_app.set_title("Fcitx5 Lotus")
        row_app.set_subtitle(_("Modern Vietnamese Input Method for Linux"))

        icon_img = Gtk.Image.new_from_icon_name("fcitx-lotus")
        icon_img.set_pixel_size(_HERO_ICON_SIZE)
        icon_img.set_valign(Gtk.Align.CENTER)
        row_app.add_prefix(icon_img)

        ver_badge = Gtk.Label(label=f"v{__version__}")
        ver_badge.add_css_class("count-pill")
        ver_badge.set_valign(Gtk.Align.CENTER)
        row_app.add_suffix(ver_badge)
        group_app.add(row_app)

        for title, url in (
            (_("GitHub Repository"), _REPO_URL),
            (_("Contributors"), _CONTRIBUTORS_URL),
        ):
            self._add_link_row(group_app, title, url)

        self.add(group_app)

        # Group 2: Report an Issue (3 repository template choices)
        group_issues = Adw.PreferencesGroup()
        group_issues.set_title(_("Report an Issue"))

        issue_options = [
            (_("Bug Report"), _("Report a bug (bug_report.yml)"), _BUG_URL),
            (_("Feature Request"), _("Suggest an enhancement (feature_request.yml)"), _FEATURE_URL),
            (
                _("Blank Issue / Question"),
                _("Open a blank issue or ask a question"),
                _BLANK_ISSUE_URL,
            ),
        ]
        for title, tip, url in issue_options:
            self._add_link_row(group_issues, title, url, tooltip=f"{tip}\n{url}")

        self.add(group_issues)

        # Group 3: Diagnostics
        group_diag = Adw.PreferencesGroup()
        group_diag.set_title(_("Diagnostics"))

        row_debug = Adw.ActionRow()
        row_debug.set_use_markup(False)
        row_debug.set_title(_("Export Debug Information"))
        row_debug.set_tooltip_text(
            _("Save system details, Lotus config, uinput logs, and fcitx5-diagnose output")
        )

        self.btn_export_debug = Gtk.Button()
        self.btn_export_debug.set_valign(Gtk.Align.CENTER)
        btn_content = Adw.ButtonContent()
        btn_content.set_icon_name("document-save-symbolic")
        btn_content.set_label(_("Export…"))
        self.btn_export_debug.set_child(btn_content)
        self.btn_export_debug.connect("clicked", self._on_export_debug_clicked)
        row_debug.add_suffix(self.btn_export_debug)
        row_debug.set_activatable_widget(self.btn_export_debug)
        group_diag.add(row_debug)

        self.add(group_diag)

        # Group 4: Licenses
        group_licenses = Adw.PreferencesGroup()
        group_licenses.set_title(_("Licenses"))

        for name, spdx in (
            ("Fcitx5 Lotus", "GPL-3.0-or-later"),
            ("Bamboo Core Engine", "MIT"),
            ("fcitx5-bamboo", "LGPL-2.1-or-later"),
        ):
            row = Adw.ActionRow()
            row.set_use_markup(False)
            row.set_title(name)
            lbl = Gtk.Label(label=spdx)
            lbl.add_css_class("dim-label")
            lbl.add_css_class("caption")
            lbl.set_valign(Gtk.Align.CENTER)
            row.add_suffix(lbl)
            group_licenses.add(row)

        self.add(group_licenses)

    def _on_export_debug_clicked(self, _button=None):
        default_name = f"fcitx5-lotus-debug-{datetime.now().strftime('%Y%m%d-%H%M%S')}.log"
        present_save_file_dialog(
            self.get_root(),
            _("Export Debug Information"),
            build_file_filters(_("Debug Log"), ("*.log", "*.txt")),
            default_name,
            self._start_export_debug,
        )

    def _start_export_debug(self, path: str):
        self.btn_export_debug.set_sensitive(False)

        def worker():
            ok = export_debug_info(path, dbus_handler=self.dbus)

            def finish():
                self.btn_export_debug.set_sensitive(True)
                self._show_toast(
                    _("Debug info exported") if ok else _("Failed to export debug info")
                )
                return GLib.SOURCE_REMOVE

            GLib.idle_add(finish)

        threading.Thread(target=worker, daemon=True).start()

    def _show_toast(self, text: str):
        show_toast_for(self, text)
