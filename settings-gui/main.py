#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Main application entry point for Lotus GTK 4 / Libadwaita Settings."""

import os
import signal
import sys

from adw_ui.utils import load_css_string
from adw_ui.window import LotusPreferencesWindow
from gi.repository import Adw, Gdk, Gio, GLib, Gtk
from i18n import _, setup_i18n

APP_ID = "org.fcitx.Fcitx5.Addon.Lotus.Settings"

APP_CSS = """
.sidebar-pane {
    background-color: @sidebar_bg_color;
    border-right: 1px solid alpha(@window_fg_color, 0.08);
}

.settings-view-stack preferencespage > scrolledwindow > viewport > clamp > box {
    padding-bottom: 76px;
}

.floating-action-bar {
    background-color: @headerbar_bg_color;
    color: @window_fg_color;
    border: 1px solid alpha(@window_fg_color, 0.14);
    border-radius: 14px;
    padding: 6px;
    box-shadow: 0 6px 18px rgba(0, 0, 0, 0.22), 0 1px 3px rgba(0, 0, 0, 0.12);
}

.floating-action-bar button.floating-btn {
    border-radius: 9px;
    padding: 5px 18px;
    min-height: 30px;
    font-weight: 600;
}

.floating-action-bar button.cancel-btn {
    background-color: alpha(@window_fg_color, 0.08);
}

.floating-action-bar button.cancel-btn:hover {
    background-color: alpha(@window_fg_color, 0.14);
}

.keycap {
    background-color: alpha(@window_fg_color, 0.08);
    color: @window_fg_color;
    border: 1px solid alpha(@window_fg_color, 0.18);
    border-bottom-width: 2px;
    border-radius: 6px;
    padding: 2px 8px;
    font-weight: 600;
    font-size: 0.9em;
    font-family: monospace;
    min-width: 22px;
}

.mode-pill,
.count-pill {
    background-color: alpha(@window_fg_color, 0.08);
    color: @window_fg_color;
    border: 1px solid alpha(@window_fg_color, 0.14);
    border-radius: 999px;
    padding: 3px 10px;
    font-size: 0.85em;
    font-weight: 500;
}

.app-avatar {
    background-color: alpha(@window_fg_color, 0.09);
    color: @window_fg_color;
    border: 1px solid alpha(@window_fg_color, 0.14);
    border-radius: 8px;
    font-weight: 600;
    font-size: 0.95em;
}

button.shortcut-pill {
    padding: 4px 8px;
    min-height: 30px;
    border-radius: 8px;
}

button.shortcut-pill.error {
    background-color: alpha(@error_color, 0.14);
    border: 1px solid alpha(@error_color, 0.6);
    color: @error_color;
}

button.shortcut-pill.error .keycap {
    background-color: alpha(@error_color, 0.16);
    border-color: alpha(@error_color, 0.45);
    color: @error_color;
}

row.error {
    background-color: alpha(@error_color, 0.08);
}

.warning-badge {
    color: @error_color;
    background-color: alpha(@error_color, 0.14);
    border-radius: 999px;
    padding: 3px 8px;
    font-size: 0.85em;
    font-weight: 600;
}

.macro-key-badge {
    background-color: alpha(@accent_bg_color, 0.14);
    color: @accent_color;
    border: 1px solid alpha(@accent_color, 0.28);
    border-radius: 6px;
    padding: 3px 8px;
    font-family: monospace;
    font-weight: 700;
    font-size: 0.9em;
}
"""


class LotusAdwApplication(Adw.Application):
    """GTK 4 / Libadwaita application managing the Lotus preferences window."""

    def __init__(self):
        super().__init__(
            application_id=APP_ID,
            flags=Gio.ApplicationFlags.DEFAULT_FLAGS,
        )
        self._css_loaded = False

    def do_startup(self):
        Adw.Application.do_startup(self)
        GLib.set_application_name(_("Lotus Settings"))
        Gtk.Window.set_default_icon_name("fcitx-lotus")

    def do_activate(self):
        self._load_css()
        win = self.props.active_window
        if not win:
            win = LotusPreferencesWindow(application=self)
        win.present()

    def _load_css(self):
        if self._css_loaded:
            return
        self._css_loaded = True

        display = Gdk.Display.get_default()
        if not display:
            return

        app_provider = Gtk.CssProvider()
        try:
            load_css_string(app_provider, APP_CSS)
            Gtk.StyleContext.add_provider_for_display(
                display,
                app_provider,
                Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
            )
        except Exception as e:
            print(f"Failed to load built-in CSS: {e}")

        # Load user DankMaterialShell / custom GTK4 overrides if present.
        css_paths = (
            os.path.expanduser("~/.config/gtk-4.0/dank-colors.css"),
            os.path.expanduser("~/.config/gtk-4.0/gtk.css"),
        )
        for path in css_paths:
            if not os.path.exists(path):
                continue
            user_provider = Gtk.CssProvider()
            try:
                user_provider.load_from_path(path)
                Gtk.StyleContext.add_provider_for_display(
                    display,
                    user_provider,
                    Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION + 1,
                )
                break
            except Exception as e:
                print(f"Failed to load custom CSS from {path}: {e}")


def _install_sigint_handler(app: Adw.Application) -> None:
    """Registers clean SIGINT termination on the GLib main loop."""

    def _on_sigint(*_args):
        app.quit()
        return GLib.SOURCE_REMOVE

    try:
        from gi.repository import GLibUnix

        GLibUnix.signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, _on_sigint)
    except (ImportError, AttributeError):
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, _on_sigint)


def main():
    """Application entry point."""
    setup_i18n()
    app = LotusAdwApplication()
    _install_sigint_handler(app)
    return app.run(sys.argv)


if __name__ == "__main__":
    sys.exit(main())
