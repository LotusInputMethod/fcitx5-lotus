# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Custom Dictionary Editor Page for Lotus (Adwaita)."""

import os

from core.dbus_handler import LotusDBusHandler
from gi.repository import Adw, Gtk, Pango
from i18n import _

from adw_ui.pages.base import StagedPage
from adw_ui.utils import (
    as_bool,
    bool_to_str,
    local_dict_path,
    make_empty_row,
    make_icon_button,
    read_dict_words,
    register_css,
    reload_dbus_config,
    remove_rows_from_group,
    save_dbus_values,
    set_entry_error,
    write_dict_words_atomic,
)
from adw_ui.widgets.section_header import attach_header_suffix_reset, build_list_section_header

try:
    from paths import DICTIONARY_PATH
except ImportError:
    DICTIONARY_PATH = "/usr/share/fcitx5/lotus/vietnamese.cm.dict"

_PAGE_SIZE = 180
_FLOWBOX_MIN_COLS = 3
_FLOWBOX_MAX_COLS = 6
_CHIP_TOOLTIP_LEN_THRESHOLD = 12

_DICT_CSS = """
.dict-chip-card {
    padding: 12px;
}

.dict-word-chip {
    background-color: alpha(@window_fg_color, 0.06);
    border: 1px solid alpha(@window_fg_color, 0.12);
    border-radius: 8px;
    padding: 4px 6px 4px 10px;
    min-height: 30px;
}

.dict-word-chip:hover {
    background-color: alpha(@window_fg_color, 0.10);
    border-color: alpha(@window_fg_color, 0.18);
}

.dict-word-chip.error {
    background-color: alpha(@error_color, 0.12);
    border-color: alpha(@error_color, 0.55);
    color: @error_color;
}

.dict-chip-label {
    font-weight: 500;
}

button.dict-chip-del {
    min-width: 22px;
    min-height: 22px;
    padding: 2px;
    opacity: 0.65;
}

button.dict-chip-del:hover {
    opacity: 1.0;
    color: @error_color;
    background-color: alpha(@error_color, 0.12);
}
"""


def _global_dict_path() -> str:
    candidates = [
        DICTIONARY_PATH,
        "/usr/share/fcitx5/lotus/vietnamese.cm.dict",
        "/usr/local/share/fcitx5/lotus/vietnamese.cm.dict",
    ]
    for path in candidates:
        if os.path.exists(path):
            return path
    return candidates[0]


class DictEditorPage(StagedPage):
    """PreferencesPage for managing Lotus custom dictionary words."""

    PAGE_ID = "dictionary"
    SUPPORTS_VALIDATION = True

    def __init__(self, dbus_handler: LotusDBusHandler, on_modified_changed=None, **kwargs):
        super().__init__(dbus_handler, on_modified_changed=on_modified_changed, **kwargs)
        register_css("dictionary", _DICT_CSS)

        self.words = []
        self._initial_words = []
        self._global_words = []
        self._enable_dict = True
        self._initial_enable_dict = True

        self._word_rows = []
        self._group_children = []
        self.flowbox = None
        self._search_text = ""
        self._display_limit = _PAGE_SIZE
        self._load_failed = False

        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        # Group 1: Custom Dictionary
        group_config = Adw.PreferencesGroup()
        group_config.set_title(_("Custom Dictionary"))
        self.btn_reset_config = attach_header_suffix_reset(group_config, self._reset_config_section)

        self.row_enable = Adw.SwitchRow()
        self.row_enable.set_use_markup(False)
        self.row_enable.set_title(_("Enable Dictionary"))
        self.row_enable.set_tooltip_text(
            _(
                "Words in the custom dictionary are excluded from 'Auto Restore Invalid Words'. "
                "Use this for names, technical terms, or words missing from the built-in list."
            )
        )
        self.row_enable.connect("notify::active", self._on_enable_toggled)
        group_config.add(self.row_enable)

        self.add(group_config)

        # Group 2: Words
        self.group_words = Adw.PreferencesGroup()
        self.group_words.set_title(_("Words"))

        self.count_badge = Gtk.Label(label="0")
        self.count_badge.add_css_class("count-pill")
        self.count_badge.add_css_class("numeric")
        self.count_badge.set_valign(Gtk.Align.CENTER)
        self.count_badge.set_tooltip_text(_("Total words"))

        controls = build_list_section_header(
            self.group_words,
            on_reset=self._reset_words_section,
            on_add=self._on_add_word_clicked,
            add_tooltip=_("Add Word"),
            on_search_changed=self._on_search_changed,
            prefix_widgets=[self.count_badge],
        )
        self.btn_reset_words = controls.btn_reset
        self.search_entry = controls.search_entry
        self.add(self.group_words)

    def _load_global_words(self) -> list[str]:
        global_path = _global_dict_path()
        if not os.path.exists(global_path):
            return []
        try:
            return read_dict_words(global_path)
        except (OSError, UnicodeDecodeError) as e:
            print(f"Failed to read built-in dictionary {global_path}: {e}")
            return []

    def load_data(self):
        """Loads EnableDictionary flag and word list, then resets baseline."""
        self._load_failed = False
        with self._updating_guard():
            config = self.dbus.get_config()
            values = config.get("values", {}) if config else {}
            enabled = as_bool(values.get("EnableDictionary", "True"), default=True)
            self._enable_dict = enabled
            self._initial_enable_dict = enabled
            self.row_enable.set_active(enabled)

            local_path = local_dict_path()
            self._global_words = self._load_global_words()
            path_to_read = local_path if os.path.exists(local_path) else _global_dict_path()

            self.words = []
            if os.path.exists(path_to_read):
                try:
                    self.words = read_dict_words(path_to_read)
                except (OSError, UnicodeDecodeError) as e:
                    self._load_failed = True
                    print(f"Failed to read dictionary {path_to_read}: {e}")

            self._initial_words = list(self.words)
            self._display_limit = _PAGE_SIZE
            self._refresh_words()

        self._notify_modified()

    def is_modified(self) -> bool:
        """Returns True if EnableDictionary or words differ from baseline."""
        return self._enable_dict != self._initial_enable_dict or sorted(self.words) != sorted(
            self._initial_words
        )

    def _words_differ_from_default(self) -> bool:
        return os.path.exists(local_dict_path()) or sorted(self.words) != sorted(self._global_words)

    def is_modified_from_default(self) -> bool:
        """Returns True if dictionary toggle or local words differ from defaults."""
        return not self._enable_dict or self._words_differ_from_default()

    def has_validation_errors(self) -> bool:
        """Returns True if loading failed or any word contains spaces."""
        return self._load_failed or any(" " in w for w in self.words)

    def validation_message(self) -> str:
        """Returns the current dictionary validation error message, if any."""
        if self._load_failed:
            return _(
                "Cannot save dictionary because loading failed earlier. Please fix the file format first."
            )
        if any(" " in w for w in self.words):
            return _("Dictionary words should not contain spaces.")
        return ""

    def apply_changes(self) -> bool:
        """Writes vietnamese.cm.dict (if needed) and EnableDictionary via D-Bus."""
        if self.has_validation_errors():
            return False

        words_changed = sorted(self.words) != sorted(self._initial_words)
        if words_changed:
            local_path = local_dict_path()
            if sorted(self.words) == sorted(self._global_words):
                if os.path.exists(local_path):
                    try:
                        os.remove(local_path)
                    except OSError as e:
                        print(f"Failed to remove custom dictionary override: {e}")
                        return False
            else:
                try:
                    write_dict_words_atomic(local_path, self.words)
                except OSError as e:
                    print(f"Failed to save dictionary: {e}")
                    return False

        # save_values calls set_config which also triggers LotusEngine::loadDictionary()
        if not save_dbus_values(self.dbus, {"EnableDictionary": bool_to_str(self._enable_dict)}):
            return False

        self._initial_enable_dict = self._enable_dict
        self._initial_words = list(self.words)
        self._notify_modified()
        return True

    def cancel_changes(self):
        """Discards staged dictionary edits and restores the baseline."""
        with self._updating_guard():
            self._enable_dict = self._initial_enable_dict
            self.row_enable.set_active(self._enable_dict)
            self.words = list(self._initial_words)
            self._display_limit = _PAGE_SIZE
            self._refresh_words()
        self._notify_modified()

    def restore_defaults(self):
        """Resets dictionary toggle to enabled and reloads built-in words in staged memory."""
        with self._updating_guard():
            self._enable_dict = True
            self.row_enable.set_active(True)
            self._global_words = self._load_global_words()
            self.words = list(self._global_words)
            self._display_limit = _PAGE_SIZE
            self._refresh_words()
        self._notify_modified()

    def _config_reset_changes(self) -> list[tuple[str, str, str]]:
        if self._enable_dict:
            return []
        return [(_("Enable Dictionary"), _("Off"), _("On"))]

    def _words_reset_changes(self) -> list[tuple[str, str, str]]:
        if not self._words_differ_from_default():
            return []
        if self._global_words:
            default_desc = _("Built-in dictionary ({count} words)").format(
                count=len(self._global_words)
            )
            added = len(set(self.words) - set(self._global_words))
            removed = len(set(self._global_words) - set(self.words))
            current_desc = _("{count} words (+{added} / -{removed})").format(
                count=len(self.words), added=added, removed=removed
            )
        else:
            default_desc = _("Empty (0 words)")
            current_desc = _("{count} words").format(count=len(self.words))
        return [(_("Custom Words"), current_desc, default_desc)]

    def get_factory_reset_changes(self) -> list[tuple[str, str, str]]:
        """Returns factory-reset diff rows for global reset confirmation."""
        return self._config_reset_changes() + self._words_reset_changes()

    def _reset_config_section(self, *_args):
        """Prompts confirmation then factory-resets and persists EnableDictionary."""
        changes = self._config_reset_changes()

        def apply_reset() -> bool:
            if not save_dbus_values(self.dbus, {"EnableDictionary": "True"}):
                self._show_toast(_("Failed to save settings. Please check if Fcitx5 is running."))
                return False

            with self._updating_guard():
                self._enable_dict = True
                self.row_enable.set_active(True)
            self._initial_enable_dict = True
            return True

        self._prompt_section_reset(_("Custom Dictionary"), changes, apply_reset)

    def _reset_words_section(self, *_args):
        """Prompts confirmation then factory-resets words (clearing local override)."""
        changes = self._words_reset_changes()

        def apply_reset() -> bool:
            local_path = local_dict_path()
            if os.path.exists(local_path):
                try:
                    os.remove(local_path)
                except OSError as e:
                    print(f"Failed to remove custom dictionary override: {e}")
                    self._show_toast(_("Failed to reset custom dictionary file."))
                    return False

            with self._updating_guard():
                self.words = list(self._global_words)
                self._display_limit = _PAGE_SIZE
                self._refresh_words()
            self._initial_words = list(self.words)
            reload_dbus_config(self.dbus)
            return True

        self._prompt_section_reset(_("Words"), changes, apply_reset)

    def _update_section_reset_buttons(self):
        self.btn_reset_config.set_visible(not self.row_enable.get_active())
        self.btn_reset_words.set_visible(self._words_differ_from_default())

    def _on_enable_toggled(self, row, _pspec):
        if self._updating:
            return
        self._enable_dict = row.get_active()
        self._notify_modified()

    def _on_search_changed(self, entry):
        self._search_text = entry.get_text().strip().lower()
        self._display_limit = _PAGE_SIZE
        self._refresh_words()

    def _on_show_more_clicked(self, _row):
        self._display_limit += _PAGE_SIZE
        self._refresh_words()

    def _refresh_words(self):
        remove_rows_from_group(self.group_words, self._group_children)
        self._word_rows.clear()
        self.flowbox = None

        filtered = [
            (i, w)
            for i, w in enumerate(self.words)
            if not self._search_text or self._search_text in w.lower()
        ]

        if self._search_text:
            self.count_badge.set_label(f"{len(filtered)} / {len(self.words)}")
        else:
            self.count_badge.set_label(str(len(self.words)))

        if not filtered:
            empty_row = make_empty_row(
                _("No Matching Words") if self._search_text else _("Dictionary Empty")
            )
            self.group_words.add(empty_row)
            self._group_children.append(empty_row)
            return

        card_row = Adw.PreferencesRow()
        card_row.set_activatable(False)
        card_row.set_focusable(False)

        card_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        card_box.add_css_class("dict-chip-card")

        flowbox = Gtk.FlowBox()
        flowbox.set_selection_mode(Gtk.SelectionMode.NONE)
        flowbox.set_homogeneous(True)
        flowbox.set_min_children_per_line(_FLOWBOX_MIN_COLS)
        flowbox.set_max_children_per_line(_FLOWBOX_MAX_COLS)
        flowbox.set_column_spacing(8)
        flowbox.set_row_spacing(8)
        flowbox.set_valign(Gtk.Align.START)
        self.flowbox = flowbox

        visible_items = filtered[: self._display_limit]
        for orig_idx, word in visible_items:
            chip = self._build_word_chip(orig_idx, word)
            flowbox.append(chip)
            self._word_rows.append(chip)

        card_box.append(flowbox)
        card_row.set_child(card_box)
        self.group_words.add(card_row)
        self._group_children.append(card_row)

        remaining = len(filtered) - len(visible_items)
        if remaining > 0:
            more_row = Adw.ActionRow()
            more_row.set_use_markup(False)
            more_row.set_title(_("Show More ({})").format(remaining))
            more_row.set_activatable(True)
            more_icon = Gtk.Image.new_from_icon_name("view-more-symbolic")
            more_row.add_suffix(more_icon)
            more_row.connect("activated", self._on_show_more_clicked)
            self.group_words.add(more_row)
            self._group_children.append(more_row)

    def _build_word_chip(self, orig_idx: int, word: str) -> Gtk.Box:
        chip = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        chip.add_css_class("dict-word-chip")

        lbl = Gtk.Label(label=word)
        lbl.set_xalign(0.0)
        lbl.set_hexpand(True)
        lbl.set_valign(Gtk.Align.CENTER)
        lbl.set_ellipsize(Pango.EllipsizeMode.END)
        lbl.add_css_class("dict-chip-label")
        if " " in word:
            chip.add_css_class("error")
            chip.set_tooltip_text(_("Dictionary words should not contain spaces."))
        elif len(word) > _CHIP_TOOLTIP_LEN_THRESHOLD:
            chip.set_tooltip_text(word)
        chip.append(lbl)

        btn_del = make_icon_button(
            "window-close-symbolic",
            _("Remove"),
            on_clicked=lambda _b, idx=orig_idx: self._remove_word(idx),
            css_classes=("flat", "circular", "dict-chip-del"),
        )
        chip.append(btn_del)
        return chip

    def _remove_word(self, index: int):
        if 0 <= index < len(self.words):
            self.words.pop(index)
            self._refresh_words()
            self._notify_modified()

    def _on_add_word_clicked(self, _button):
        dialog = Adw.AlertDialog(
            heading=_("Add Word"),
            body=_("Enter a word without spaces:"),
        )

        entry = Gtk.Entry()
        entry.set_placeholder_text(_("e.g. khongdau"))
        entry.set_activates_default(True)
        dialog.set_extra_child(entry)

        dialog.add_response("cancel", _("Cancel"))
        dialog.add_response("add", _("Add"))
        dialog.set_response_appearance("add", Adw.ResponseAppearance.SUGGESTED)
        dialog.set_default_response("add")
        dialog.set_close_response("cancel")
        dialog.set_response_enabled("add", False)

        def validate(ent):
            raw = ent.get_text().strip()
            has_spaces = " " in raw
            exists = raw in self.words
            if has_spaces:
                err_msg = _("Words cannot contain spaces")
            elif exists:
                err_msg = _("Word already exists")
            else:
                err_msg = ""
            set_entry_error(ent, err_msg)
            dialog.set_response_enabled("add", bool(raw) and not has_spaces and not exists)

        entry.connect("changed", validate)

        def on_response(_dlg, response):
            if response != "add":
                return
            w = entry.get_text().strip()
            if w and " " not in w and w not in self.words:
                self.words.insert(0, w)
                self._refresh_words()
                self._notify_modified()

        dialog.connect("response", on_response)
        dialog.present(self.get_root())
