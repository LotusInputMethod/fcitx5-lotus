# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared GTK 4 / Libadwaita utilities, config helpers, and file-dialog wrappers."""

import os
import tempfile

from gi.repository import Adw, Gdk, Gio, Gtk
from i18n import _

_REGISTERED_CSS_KEYS: set[tuple[str, int]] = set()


def as_bool(value, default: bool = False) -> bool:
    """Converts a string or boolean configuration value to ``bool``."""
    if isinstance(value, bool):
        return value
    if value is None:
        return default
    return str(value).strip().lower() == "true"


def bool_to_str(value: bool) -> str:
    """Encodes a boolean configuration value as ``"True"`` or ``"False"``."""
    return "True" if value else "False"


def get_explicit_method(obj, name: str):
    """Returns bound method only if defined on a real class in the MRO (stub-safe)."""
    for cls in type(obj).__mro__:
        if name in cls.__dict__:
            method = getattr(obj, name, None)
            return method if callable(method) else None
    return None


def load_css_string(provider: Gtk.CssProvider, css_text: str) -> None:
    """Loads CSS text compatibly across GTK 4 minor versions."""
    if hasattr(provider, "load_from_string"):
        provider.load_from_string(css_text)
    else:
        provider.load_from_data(css_text.encode("utf-8"))


def register_css(key: str, css_text: str) -> None:
    """Registers an application-priority CSS stylesheet once per display."""
    display = Gdk.Display.get_default()
    if display is None:
        return
    token = (key, id(display))
    if token in _REGISTERED_CSS_KEYS:
        return
    _REGISTERED_CSS_KEYS.add(token)
    provider = Gtk.CssProvider()
    load_css_string(provider, css_text)
    Gtk.StyleContext.add_provider_for_display(
        display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
    )


def clear_children(container) -> None:
    """Removes all direct child widgets from a GTK 4 container."""
    while (child := container.get_first_child()) is not None:
        container.remove(child)


def remove_rows_from_group(group: Adw.PreferencesGroup, rows: list) -> None:
    """Removes tracked row widgets from a PreferencesGroup and clears the list."""
    for row in rows:
        group.remove(row)
    rows.clear()


def make_empty_row(title: str) -> Adw.ActionRow:
    """Creates a plain non-activatable ActionRow for empty-list placeholders."""
    row = Adw.ActionRow()
    row.set_use_markup(False)
    row.set_title(title)
    row.set_activatable(False)
    return row


def make_icon_button(
    icon_name: str,
    tooltip: str,
    on_clicked=None,
    css_classes: tuple[str, ...] = ("flat",),
    valign=Gtk.Align.CENTER,
) -> Gtk.Button:
    """Creates a compact icon button with tooltip, CSS classes, and optional click handler."""
    btn = Gtk.Button()
    btn.set_icon_name(icon_name)
    btn.set_tooltip_text(tooltip)
    btn.set_valign(valign)
    for cls in css_classes:
        btn.add_css_class(cls)
    if on_clicked is not None:
        btn.connect("clicked", on_clicked)
    return btn


def make_delete_button(on_clicked, tooltip: str | None = None) -> Gtk.Button:
    """Creates a flat destructive trash icon button for row deletion."""
    return make_icon_button(
        "user-trash-symbolic",
        tooltip if tooltip is not None else _("Remove"),
        on_clicked=on_clicked,
        css_classes=("flat", "destructive-action"),
    )


def set_entry_error(entry: Gtk.Entry, error_message: str = "") -> None:
    """Applies or clears the ``error`` CSS class and tooltip on a Gtk.Entry."""
    if error_message:
        entry.add_css_class("error")
        entry.set_tooltip_text(error_message)
    else:
        entry.remove_css_class("error")
        entry.set_tooltip_text("")


def show_toast_for(widget: Gtk.Widget, text: str) -> None:
    """Shows a toast on the nearest ToastOverlay ancestor or root window."""
    toast = Adw.Toast.new(text)
    cur = widget.get_parent()
    while cur is not None:
        if isinstance(cur, Adw.ToastOverlay) or get_explicit_method(cur, "add_toast") is not None:
            cur.add_toast(toast)
            return
        cur = cur.get_parent()
    root = widget.get_root()
    if root is not None and get_explicit_method(root, "add_toast") is not None:
        root.add_toast(toast)
    else:
        print(text)


def save_dbus_values(dbus_handler, updates: dict) -> bool:
    """Persists partial key-value config updates using ``save_values`` or fallback."""
    if not updates:
        return True
    if hasattr(dbus_handler, "save_values"):
        return bool(dbus_handler.save_values(updates))
    config_data = dbus_handler.get_config()
    if not config_data:
        return False
    values = dict(config_data.get("values", {}))
    values.update(updates)
    return bool(dbus_handler.set_config(values))


def reload_dbus_config(dbus_handler) -> None:
    """Re-applies current config values so the engine reloads on-disk dictionary files."""
    cfg = dbus_handler.get_config()
    if cfg and isinstance(cfg.get("values"), dict):
        dbus_handler.set_config(cfg["values"])


def local_dict_path() -> str:
    """Returns the user custom dictionary file path under ``XDG_DATA_HOME``."""
    xdg_data_home = os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share"))
    return os.path.join(xdg_data_home, "fcitx5/lotus/vietnamese.cm.dict")


def read_dict_words(path: str) -> list[str]:
    """Reads non-empty, non-comment words from a dictionary file."""
    words = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            word = line.strip()
            if word and not word.startswith("#"):
                words.append(word)
    return words


def write_dict_words_atomic(path: str, words: list[str] | str) -> None:
    """Atomically writes dictionary words (list or raw text) to ``path``."""
    target_dir = os.path.dirname(path)
    if target_dir:
        os.makedirs(target_dir, exist_ok=True)
    temp_name = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", dir=target_dir or ".", encoding="utf-8", delete=False
        ) as tf:
            if isinstance(words, str):
                tf.write(words)
            else:
                for raw in words:
                    word = str(raw).strip()
                    if word:
                        tf.write(f"{word}\n")
            temp_name = tf.name
        os.replace(temp_name, path)
    except Exception:
        if temp_name and os.path.exists(temp_name):
            try:
                os.remove(temp_name)
            except OSError:
                pass
        raise


def build_file_filters(filter_name: str, patterns: tuple[str, ...]) -> Gio.ListStore:
    """Builds a ``Gio.ListStore`` containing a primary filter followed by All Files."""
    filters = Gio.ListStore.new(Gtk.FileFilter)
    primary = Gtk.FileFilter()
    primary.set_name(filter_name)
    for pattern in patterns:
        primary.add_pattern(pattern)
    filters.append(primary)

    all_filter = Gtk.FileFilter()
    all_filter.set_name(_("All Files"))
    all_filter.add_pattern("*")
    filters.append(all_filter)
    return filters


def _is_dialog_cancelled(err: Exception) -> bool:
    domain = getattr(err, "domain", None)
    code = getattr(err, "code", None)
    if domain == "gtk-dialog-error-quark" and hasattr(Gtk, "DialogError"):
        return code in (Gtk.DialogError.DISMISSED, Gtk.DialogError.CANCELLED)
    if domain == "g-io-error-quark" and hasattr(Gio, "IOErrorEnum"):
        return code == Gio.IOErrorEnum.CANCELLED
    msg = str(err).lower()
    return "dismissed" in msg or "cancel" in msg


def present_save_file_dialog(
    parent,
    title: str,
    filters: Gio.ListStore,
    initial_name: str,
    on_selected,
) -> Gtk.FileDialog:
    """Presents a save ``Gtk.FileDialog`` and passes the chosen path to ``on_selected``."""
    file_dialog = Gtk.FileDialog()
    file_dialog.set_title(title)
    file_dialog.set_filters(filters)
    file_dialog.set_initial_name(initial_name)

    def on_save(dialog, result):
        try:
            gfile = dialog.save_finish(result)
        except Exception as e:
            if not _is_dialog_cancelled(e):
                print(f"File save dialog error: {e}")
            return
        if gfile and gfile.get_path():
            on_selected(gfile.get_path())

    file_dialog.save(parent, None, on_save)
    return file_dialog


def present_open_file_dialog(
    parent,
    title: str,
    filters: Gio.ListStore,
    on_selected,
) -> Gtk.FileDialog:
    """Presents an open ``Gtk.FileDialog`` and passes the chosen path to ``on_selected``."""
    file_dialog = Gtk.FileDialog()
    file_dialog.set_title(title)
    file_dialog.set_filters(filters)

    def on_open(dialog, result):
        try:
            gfile = dialog.open_finish(result)
        except Exception as e:
            if not _is_dialog_cancelled(e):
                print(f"File open dialog error: {e}")
            return
        if gfile and gfile.get_path():
            on_selected(gfile.get_path())

    file_dialog.open(parent, None, on_open)
    return file_dialog
