# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Mode Manager Page for per-application input mode configuration.
"""

import os
import re
from qtpy.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QListWidget,
    QListWidgetItem,
    QLabel,
    QFrame,
    QPushButton,
    QLineEdit,
    QScrollArea,
    QDialog,
    QTabWidget,
    QFileDialog,
    QComboBox,
    QGridLayout,
    QMessageBox,
)
from qtpy.QtCore import Qt, QSize, Signal
from qtpy.QtGui import QIcon
from i18n import _
from ui.pages.dynamic_settings import CardWidget
from core.dbus_handler import LotusDBusHandler

# Mode constants as defined in C++ LotusEngine
MODE_OFF = 0
MODE_SMOOTH = 1
MODE_SLOW = 2
MODE_SURROUNDING = 4
MODE_PREEDIT = 5
MODE_EMOJI = 6
MODE_MINECRAFT = 8
MODE_DEFAULT = -1  # UI special value for "Use Global Default"

MODE_INFO = {
    MODE_DEFAULT: {"title": "Default Typing", "icon": "preferences-system"},
    MODE_OFF: {"title": "OFF", "icon": "input-keyboard"},
    MODE_SMOOTH: {"title": "Uinput (Smooth)", "icon": "input-keyboard"},
    MODE_SLOW: {"title": "Uinput (Slow)", "icon": "input-keyboard"},
    MODE_SURROUNDING: {"title": "Surrounding Text", "icon": "text-field"},
    MODE_PREEDIT: {"title": "Preedit", "icon": "text-field"},
    MODE_EMOJI: {"title": "Emoji Picker", "icon": "face-smile"},
    MODE_MINECRAFT: {"title": "Minecraft", "icon": "onboard"},
}



class ModeCard(QFrame):
    """A card for selecting an input mode."""

    clicked = Signal(int)

    def __init__(self, mode: int, selected: bool = False, parent=None):
        super().__init__(parent)
        self.mode = mode
        self.selected = selected
        self.setObjectName("ModeCard")
        self.setCursor(Qt.PointingHandCursor)
        self._setup_ui()
        self.update_style()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 15, 10, 15)
        layout.setSpacing(5)

        info = MODE_INFO[self.mode]
        title_label = QLabel(_(info["title"]))
        title_label.setObjectName("ModeCardTitle")
        title_label.setAlignment(Qt.AlignCenter)
        title_label.setWordWrap(True)
        title_label.setStyleSheet("font-weight: bold; font-size: 13px;")

        layout.addWidget(title_label)

    def update_style(self):
        if self.selected:
            self.setStyleSheet(
                """
                QFrame#ModeCard {
                    border: 1.5px solid palette(highlight);
                    background: palette(highlight);
                    border-radius: 8px;
                }
                QLabel { color: palette(highlighted-text); }
            """
            )
        else:
            self.setStyleSheet(
                """
                QFrame#ModeCard {
                    border: 1.5px solid palette(mid);
                    background: palette(alternate-base);
                    border-radius: 8px;
                }
            """
            )

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit(self.mode)


class AddAppDialog(QDialog):
    """Dialog to add a new application to the rules list."""

    def __init__(self, icon_cache=None, existing_apps=None, parent=None):
        super().__init__(parent)
        self.setWindowTitle(_("Add Application"))
        self.setMinimumSize(500, 450)
        self.selected_app = None
        self._icon_cache = icon_cache or {}
        self.existing_apps = set(existing_apps or [])
        self._setup_ui()
        self._load_running_apps()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)

        header_title = QLabel(_("Add Application"))
        header_title.setStyleSheet("font-size: 18px; font-weight: bold;")
        header_subtitle = QLabel(_("Assign an input mode to a specific application."))
        header_subtitle.setStyleSheet("opacity: 0.7;")

        layout.addWidget(header_title)
        layout.addWidget(header_subtitle)

        self.tabs = QTabWidget()
        layout.addWidget(self.tabs)

        # Tab 1: Running Apps
        self.running_tab = QWidget()
        running_layout = QVBoxLayout(self.running_tab)
        
        search_layout = QHBoxLayout()
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText(_("Search process name..."))
        self.search_input.textChanged.connect(self._filter_running_apps)
        
        self.btn_refresh = QPushButton(QIcon.fromTheme("view-refresh"), "")
        self.btn_refresh.setToolTip(_("Refresh Process List"))
        self.btn_refresh.setFlat(True)
        self.btn_refresh.clicked.connect(self._load_running_apps)
        
        search_layout.addWidget(self.search_input, 1)
        search_layout.addWidget(self.btn_refresh)
        running_layout.addLayout(search_layout)

        self.running_list = QListWidget()
        self.running_list.setIconSize(QSize(32, 32))
        self.running_list.itemClicked.connect(self._on_app_selected)
        self.running_list.itemDoubleClicked.connect(self._on_item_double_clicked)
        running_layout.addWidget(self.running_list)
        
        self.tabs.addTab(self.running_tab, _("Running"))

        # Tab 2: Manual Input
        self.manual_tab = QWidget()
        manual_layout = QVBoxLayout(self.manual_tab)
        self.manual_input = QLineEdit()
        self.manual_input.setPlaceholderText(_("Enter application name or path..."))
        self.manual_input.returnPressed.connect(self._on_add_clicked)
        manual_layout.addWidget(self.manual_input)
        manual_layout.addStretch()
        self.tabs.addTab(self.manual_tab, _("Manual input"))

        # Bottom Buttons
        bottom_layout = QHBoxLayout()
        self.selection_label = QLabel(_("No application selected"))
        self.selection_label.setStyleSheet("opacity: 0.7;")
        
        self.btn_cancel = QPushButton(QIcon.fromTheme("dialog-cancel"), _("&Cancel"))
        self.btn_cancel.clicked.connect(self.reject)
        
        self.btn_add = QPushButton(QIcon.fromTheme("dialog-ok"), _("&Add"))
        self.btn_add.setObjectName("Primary")
        self.btn_add.setEnabled(False)
        self.btn_add.clicked.connect(self._on_add_clicked)

        bottom_layout.addWidget(self.selection_label)
        bottom_layout.addStretch()
        bottom_layout.addWidget(self.btn_cancel)
        bottom_layout.addWidget(self.btn_add)
        layout.addLayout(bottom_layout)

    def _load_running_apps(self):
        """Loads running processes from /proc, filtered for user applications."""
        apps = []
        try:
            current_uid = os.getuid()
            for pid_dir in os.listdir("/proc"):
                if not pid_dir.isdigit():
                    continue
                pid = int(pid_dir)
                try:
                    # Filter by UID (only show current user's processes)
                    try:
                        stat_info = os.stat(f"/proc/{pid}")
                        if stat_info.st_uid != current_uid:
                            continue
                    except (PermissionError, FileNotFoundError):
                        continue

                    with open(f"/proc/{pid}/comm", "r") as f:
                        name = f.read().strip()
                    with open(f"/proc/{pid}/cmdline", "r") as f:
                        cmdline = f.read().replace("\x00", " ").strip()
                    
                    if not cmdline:
                        continue
                        
                    exe = ""
                    try:
                        exe = os.readlink(f"/proc/{pid}/exe")
                    except (PermissionError, FileNotFoundError):
                        continue # Probably a kernel thread
                    
                    # Clean process names for NixOS
                    if name.startswith('.'):
                        exe_base = os.path.basename(exe)
                        if exe_base.startswith('.') and exe_base.endswith('-wrapped'):
                            name = exe_base[1:-8]
                        elif name.startswith('.' + exe_base) or name == ('.' + exe_base)[:15]:
                            name = exe_base
                        else:
                            clean = name[1:]
                            # On NixOS, wrapped application names from /proc/<pid>/comm can be truncated.
                            # We check for partial suffixes of "-wrapped", from longest to shortest.
                            base_suffix = "-wrapped"
                            for i in range(len(base_suffix), 1, -1):
                                if clean.endswith(base_suffix[:i]):
                                    clean = clean[:-i]
                                    break
                            if clean:
                                name = clean

                    # Exclude common system/background paths
                    exclude_paths = ["/usr/lib", "/usr/libexec", "/lib", "/systemd", "/usr/sbin"]
                    if any(exe.startswith(p) for p in exclude_paths):
                        continue
                    
                    # Heuristic: Exclude common background process patterns
                    # These processes run as user but are typically not "apps" for rules
                    bg_patterns = [
                        "_agent", "_helper", "_daemon", "_resource", "_server",
                        "-agent", "-helper", "-daemon", "-sandbox", "-proxy",
                        "akonadi", "kactivitymanagerd", "kaccess", "krunner",
                        "ksmserver", "kwin_", "kglobalaccel", "org.kde.",
                        "gnome-shell", "dbus-", "at-spi", "pipewire", "pulseaudio",
                        "xdg-", "gvfs", "tracker-", "evolution-", "mission-control",
                        "telepathy", "dconf", "applet", "notify-osd", "indicator-",
                        "plasmashell", "xwayland", "wireplumber", "xsettingsd",
                        "xembedsniproxy", "gmenudbusmenuproxy", "kalendarac",
                        "ksystemstats", "ksecretd", "kwalletd", "kded", "startplasma", "bwrap"
                    ]
                    basename = os.path.basename(exe).lower()
                    if any(p in name.lower() or p in basename for p in bg_patterns):
                        continue
                    
                    # Exclude the settings-gui itself and python interpreters with no script
                    if ("main.py" in cmdline or "settings-gui" in cmdline) and "python" in exe:
                        continue
                    
                    if name in self.existing_apps:
                        continue
                    
                    # If it's a python command but unknown script, ignore it
                    if basename.startswith("python") and len(cmdline.split()) < 2:
                        continue

                    if name and exe:
                        apps.append({"name": name, "exe": exe, "pid": pid})
                except (PermissionError, FileNotFoundError, ProcessLookupError):
                    continue
                except Exception:
                    continue
        except Exception as e:
            print(f"Error listing /proc: {e}")

        unique_apps = {}
        for app in apps:
            key = app["exe"]
            if key not in unique_apps:
                unique_apps[key] = app
        
        sorted_apps = sorted(unique_apps.values(), key=lambda x: x["name"].lower())
        self.full_app_list = sorted_apps
        self._populate_list(sorted_apps)

    def _populate_list(self, apps):
        self.running_list.clear()
        for app in apps:
            item = QListWidgetItem()
            item.setText(f"{app['name']}\n{app['exe']}")
            item.setData(Qt.UserRole, app)
            
            icon_name = self._icon_cache.get(app["name"].lower())
            if not icon_name:
                icon_name = self._icon_cache.get(os.path.basename(app["exe"]).lower(), app["name"].lower())
            
            item.setIcon(QIcon.fromTheme(icon_name, QIcon.fromTheme("application-x-executable")))
            self.running_list.addItem(item)

    def _filter_running_apps(self, text):
        filtered = [a for a in self.full_app_list if text.lower() in a["name"].lower() or text.lower() in a["exe"].lower()]
        self._populate_list(filtered)

    def _on_app_selected(self, item):
        app = item.data(Qt.UserRole)
        self.selected_app = app["name"]
        self.selection_label.setText(f"{_('Selected:')} {self.selected_app}")
        self.btn_add.setEnabled(True)

    def _on_item_double_clicked(self, item):
        self._on_app_selected(item)
        self._on_add_clicked()

    def _on_add_clicked(self):
        if self.tabs.currentIndex() == 1: # Manual
            self.selected_app = self.manual_input.text().strip()
            if not self.selected_app:
                return
        self.accept()


class ModeManagerPage(QWidget):
    """Main Mode Manager page."""

    def __init__(self, dbus_handler: LotusDBusHandler, parent=None):
        super().__init__(parent)
        self.dbus = dbus_handler
        self.app_rules = {}
        self.original_app_rules = {}
        self.original_global_mode = ""
        self.selected_app = None
        self.current_app_mode = MODE_DEFAULT
        self._icon_cache = {}
        self._setup_ui()
        self.load_data()

    def _setup_ui(self):
        self.layout = QHBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)

        # Left Sidebar (Expanded)
        self.sidebar_widget = QWidget()
        self.sidebar_widget.setFixedWidth(240)
        self.sidebar_layout = QVBoxLayout(self.sidebar_widget)
        self.sidebar_layout.setContentsMargins(15, 20, 15, 20)
        self.app_search = QLineEdit()
        self.app_search.setPlaceholderText(_("Search applications..."))
        self.app_search.textChanged.connect(self._filter_apps)
        self.sidebar_layout.addWidget(self.app_search)

        self.sidebar_layout.setSpacing(10)

        self.app_list = QListWidget()
        self.app_list.setIconSize(QSize(24, 24))
        self.app_list.itemClicked.connect(self._on_app_selected)
        self.sidebar_layout.addWidget(self.app_list)

        self.btn_add_app = QPushButton(QIcon.fromTheme("list-add"), _("Add Application"))
        self.btn_remove_app = QPushButton(QIcon.fromTheme("list-remove"), _("Remove"))
        self.btn_remove_app.setEnabled(False)
        
        self.btn_add_app.clicked.connect(self._on_add_app)
        self.btn_remove_app.clicked.connect(self._on_remove_app)
        
        self.sidebar_layout.addWidget(self.btn_add_app)
        self.sidebar_layout.addWidget(self.btn_remove_app)

        self.layout.addWidget(self.sidebar_widget)

        # Right Content Area
        self.content_widget = QScrollArea()
        self.content_widget.setWidgetResizable(True)
        self.content_widget.setFrameShape(QFrame.NoFrame)
        
        self.main_container = QWidget()
        self.main_layout = QVBoxLayout(self.main_container)
        self.main_layout.setContentsMargins(30, 20, 30, 30)
        self.main_layout.setSpacing(20)

        title = QLabel(_("Applications"))
        title.setObjectName("CategoryTitle")
        self.main_layout.addWidget(title)

        # 1. Global Mode Section (Simplified Card)
        self.global_card = CardWidget("")
        global_layout = QHBoxLayout()
        global_layout.addWidget(QLabel(_("Global Default Mode:")))
        self.combo_global_mode = QComboBox()
        global_modes = [
            MODE_SMOOTH, MODE_SLOW, MODE_MINECRAFT,
            MODE_SURROUNDING, MODE_PREEDIT, MODE_EMOJI,
            MODE_OFF
        ]
        for m in global_modes:
            self.combo_global_mode.addItem(_(MODE_INFO[m]["title"]), MODE_INFO[m]["title"])
            
        self.combo_global_mode.currentIndexChanged.connect(self._on_global_mode_changed)
        global_layout.addWidget(self.combo_global_mode)
        self.global_card.content_layout.addLayout(global_layout)
        self.main_layout.addWidget(self.global_card)

        # 2. Selected App Card (Empty Title)
        self.app_settings_card = CardWidget("")
        self.app_settings_layout = QVBoxLayout()
        
        # App Info Header
        self.app_header_layout = QHBoxLayout()
        self.app_icon_label = QLabel()
        self.app_icon_label.setFixedSize(48, 48)
        self.app_name_label = QLabel(_("Select an application"))
        self.app_name_label.setStyleSheet("font-size: 16px; font-weight: bold;")
        self.app_header_layout.addWidget(self.app_icon_label)
        self.app_header_layout.addWidget(self.app_name_label)
        self.app_header_layout.addStretch()
        self.app_settings_layout.addLayout(self.app_header_layout)
        self.app_settings_layout.addSpacing(10)

        # Grid for App Modes
        self.mode_grid = QGridLayout()
        self.mode_grid.setSpacing(10)
        self.mode_cards = {}
        
        grid_modes = [
            MODE_SMOOTH, MODE_SLOW,
            MODE_MINECRAFT, MODE_SURROUNDING,
            MODE_PREEDIT, MODE_EMOJI,
            MODE_OFF, MODE_DEFAULT
        ]
        for i, m in enumerate(grid_modes):
            card = ModeCard(m)
            card.clicked.connect(self._on_app_mode_changed)
            self.mode_cards[m] = card
            self.mode_grid.addWidget(card, i // 2, i % 2)
            
        self.app_settings_layout.addLayout(self.mode_grid)
        self.app_settings_card.content_layout.addLayout(self.app_settings_layout)
        self.main_layout.addWidget(self.app_settings_card)
        self.main_layout.addStretch()

        # Initial visibility
        self.app_settings_card.setVisible(False)

        self.content_widget.setWidget(self.main_container)
        self.layout.addWidget(self.content_widget)

    def load_data(self):
        """Loads rules from config and global mode from DBus."""
        self.app_rules = {}
        try:
            data = self.dbus.get_sub_config_list("app_rules", "Rules")
            for item in data:
                app = item.get("App", "")
                mode = int(item.get("Mode", 0))
                if app:
                    self.app_rules[app] = mode
        except Exception as e:
            print(f"Error loading app rules via DBus: {e}")

        # Sync Global Mode
        config = self.dbus.get_config()
        mode_str = config.get("values", {}).get("Mode", "Uinput (Smooth)")
        self.combo_global_mode.blockSignals(True)
        idx = self.combo_global_mode.findData(mode_str)
        if idx >= 0:
            self.combo_global_mode.setCurrentIndex(idx)
        self.combo_global_mode.blockSignals(False)
        self.original_global_mode = mode_str
        
        self._populate_app_list()
        self.original_app_rules = self.app_rules.copy()

    def _populate_app_list(self):
        self.app_list.clear()
        self._scan_desktop_files()
        apps_to_show = set(self.app_rules.keys())
        if self.selected_app:
            apps_to_show.add(self.selected_app)

        for app in sorted(apps_to_show):
            mode = self.app_rules.get(app, MODE_DEFAULT)
            mode_text = _(MODE_INFO.get(mode, MODE_INFO[MODE_SMOOTH])["title"])
            
            item = QListWidgetItem()
            item.setText(f"{app}\n{mode_text}")
            item.setData(Qt.UserRole, (app, mode))
            item.setIcon(self._resolve_icon(app))
            self.app_list.addItem(item)
            
            if app == self.selected_app:
                self.app_list.setCurrentItem(item)

    def _scan_desktop_files(self):
        """Builds a robust map of app identifiers to icon names."""
        search_paths = [
            "/usr/share/applications",
            os.path.expanduser("~/.local/share/applications"),
            "/var/lib/flatpak/exports/share/applications",
            os.path.expanduser("~/.local/share/flatpak/exports/share/applications"),
            "/var/lib/snapd/desktop/applications",
        ]

        xdg_data_dirs = os.environ.get("XDG_DATA_DIRS", "").split(":")
        for directory in xdg_data_dirs:
            if directory:
                xdg_applications_path = os.path.join(directory, "applications")
                if xdg_applications_path not in search_paths:
                    search_paths.append(xdg_applications_path)
        
        # Priority 1: Direct binary names from Exec line
        # Priority 2: Desktop filenames (e.g. com.discordapp.Discord -> Discord)
        # Priority 3: Application Names
        
        for p in search_paths:
            if not os.path.isdir(p):
                continue
            for f in os.listdir(p):
                if not f.endswith(".desktop"):
                    continue
                try:
                    desktop_id = f[:-8] # remove .desktop
                    with open(os.path.join(p, f), "r", encoding="utf-8") as df:
                        content = df.read()
                        
                        icon_match = re.search(r"^Icon=([^\n]+)", content, re.MULTILINE)
                        if not icon_match: continue
                        icon = icon_match.group(1).strip()
                        
                        # Map by desktop ID (e.g. discord)
                        self._icon_cache[desktop_id.lower()] = icon
                        if "." in desktop_id: # handle com.discordapp.Discord
                            self._icon_cache[desktop_id.split(".")[-1].lower()] = icon
                        
                        name_match = re.search(r"^Name=([^\n]+)", content, re.MULTILINE)
                        if name_match:
                            self._icon_cache[name_match.group(1).strip().lower()] = icon

                        exec_match = re.search(r"^Exec=([^\n]+)", content, re.MULTILINE)
                        if exec_match:
                            exec_line = exec_match.group(1).strip()
                            # Robustly extract binary name (handle quotes and arguments)
                            if exec_line.startswith('"'):
                                binary_path = exec_line[1:].split('"')[0]
                            else:
                                binary_path = exec_line.split(" ")[0]
                            
                            binary_name = os.path.basename(binary_path).lower()
                            self._icon_cache[binary_name] = icon
                            
                            if binary_name == "flatpak" and " --command=" in exec_line:
                                cmd_match = re.search(r"--command=([^ ]+)", exec_line)
                                if cmd_match:
                                    self._icon_cache[cmd_match.group(1).lower()] = icon
                except (PermissionError, FileNotFoundError):
                    continue
                except Exception as e:
                    # Only log truly unexpected errors
                    if not isinstance(e, (UnicodeDecodeError, re.error)):
                        print(f"Error parsing desktop file {f}: {e}")
                    continue
        
        # Manual Overrides for stubborn apps
        manual_icons = {
            "discord": "discord",
            "upnote": "upnote",
            "antigravity": "preferences-desktop-accessibility",
        }
        for k, v in manual_icons.items():
            if k not in self._icon_cache:
                self._icon_cache[k] = v

    def _resolve_icon(self, app_name):
        """Resolves icon name for a given app handle."""
        app_lower = app_name.lower()
        icon_name = self._icon_cache.get(app_lower)
        
        if not icon_name:
            # Try removing extension or path if it's a full path
            basename = os.path.basename(app_name).lower()
            if "." in basename:
                basename = basename.split(".")[0]
            icon_name = self._icon_cache.get(basename, basename)
            
        return QIcon.fromTheme(icon_name, QIcon.fromTheme("application-x-executable"))

    def _filter_apps(self, text):
        for i in range(self.app_list.count()):
            item = self.app_list.item(i)
            item.setHidden(text.lower() not in item.text().split("\n")[0].lower())

    def _on_app_selected(self, item):
        app_name, mode = item.data(Qt.UserRole)
        self.selected_app = app_name
        self.current_app_mode = mode
        
        self.app_name_label.setText(app_name)
        self.app_icon_label.setPixmap(self._resolve_icon(app_name).pixmap(48, 48))
        self.app_settings_card.setVisible(True)
        self.btn_remove_app.setEnabled(True) # Enable Remove
        self._update_mode_cards()

    def _on_global_mode_changed(self, index):
        if not self.isVisible():
            return
        
        self._notify_changed()


    def _on_app_mode_changed(self, mode):
        self.current_app_mode = mode
        if mode == MODE_DEFAULT:
            if self.selected_app in self.app_rules:
                del self.app_rules[self.selected_app]
        else:
            self.app_rules[self.selected_app] = mode
        
        self._update_mode_cards()
        self._populate_app_list()
        self._notify_changed()

    def _update_mode_cards(self):
        for m, card in self.mode_cards.items():
            card.selected = (m == self.current_app_mode)
            card.update_style()

    def _on_add_app(self):
        dialog = AddAppDialog(self._icon_cache, list(self.app_rules.keys()), self)
        if dialog.exec():
            new_app = dialog.selected_app
            if new_app not in self.app_rules:
                self.app_rules[new_app] = MODE_SMOOTH
            self.selected_app = new_app
            self._populate_app_list()
            self._notify_changed()

    def _on_remove_app(self):
        if not self.selected_app:
            return
            
        reply = QMessageBox.question(
            self, _("Confirm Remove"),
            _("Remove rules for this application?"),
            QMessageBox.Yes | QMessageBox.No
        )
        if reply == QMessageBox.No:
            return
            
        if self.selected_app in self.app_rules:
            del self.app_rules[self.selected_app]
        
        self.selected_app = None
        self.app_settings_card.setVisible(False)
        self.btn_remove_app.setEnabled(False)
        self._populate_app_list()
        self._notify_changed()

    def _notify_changed(self):
        main_win = self.window()
        if hasattr(main_win, "on_changed"):
            main_win.on_changed()

    def is_modified(self):
        """Returns True if the current state differs from the initial loaded state."""
        return (
            self.app_rules != self.original_app_rules
            or self.combo_global_mode.currentData() != self.original_global_mode
        )

    def is_modified_from_default(self):
        """Returns True if the current state differs from the default state."""
        return (
            len(self.app_rules) > 0
            or self.combo_global_mode.currentData() != "Uinput (Smooth)"
        )

    def save_data(self):
        try:
            if self.combo_global_mode.currentData() != self.original_global_mode:
                config_data = self.dbus.get_config()
                if config_data:
                    latest_values = config_data.get("values", {})
                    latest_values["Mode"] = self.combo_global_mode.currentData()
                    self.dbus.set_config(latest_values)

            data = []
            for app, mode in sorted(self.app_rules.items()):
                data.append({"App": app, "Mode": str(mode)})
            
            self.dbus.set_sub_config_list("app_rules", "Rules", data)
            self.original_app_rules = self.app_rules.copy()
            self.original_global_mode = self.combo_global_mode.currentData()

        except Exception as e:
            print(f"Error saving app rules via DBus: {e}")

    def restore_defaults(self):
        self.load_data()