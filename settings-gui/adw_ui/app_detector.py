# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Desktop entry metadata indexing and running-application detection helpers."""

import json
import os
import re
import subprocess

_EXCLUDED_PROC_PREFIXES = (
    "/usr/lib",
    "/usr/libexec",
    "/lib",
    "/systemd",
    "/usr/sbin",
)

_BG_PROCESS_PATTERNS = (
    "_agent",
    "_helper",
    "_daemon",
    "_resource",
    "_server",
    "-agent",
    "-helper",
    "-daemon",
    "-sandbox",
    "-proxy",
    "akonadi",
    "kactivitymanagerd",
    "kaccess",
    "krunner",
    "ksmserver",
    "kwin_",
    "kglobalaccel",
    "org.kde.",
    "gnome-shell",
    "dbus-",
    "at-spi",
    "pipewire",
    "pulseaudio",
    "xdg-",
    "gvfs",
    "tracker-",
    "evolution-",
    "mission-control",
    "telepathy",
    "dconf",
    "applet",
    "notify-osd",
    "indicator-",
    "plasmashell",
    "xwayland",
    "wireplumber",
    "xsettingsd",
    "xembedsniproxy",
    "gmenudbusmenuproxy",
    "kalendarac",
    "ksystemstats",
    "ksecretd",
    "kwalletd",
    "kded",
    "startplasma",
    "bwrap",
    "fcitx5",
)

_FALLBACK_ICONS = {
    "discord": "discord",
    "upnote": "upnote",
    "antigravity": "preferences-desktop-accessibility",
}

_COMPOSITOR_TIMEOUT_S = 1.5
_XPROP_TIMEOUT_S = 0.5
_MAX_X11_WINDOWS = 40
_COMM_MAX_LEN = 15


def clean_proc_name(name: str, exe: str) -> str:
    """Strips Nix/wrapper ``.foo-wrapped`` noise from ``/proc/<pid>/comm``."""
    if name.startswith(".") and exe:
        exe_base = os.path.basename(exe)
        if exe_base.startswith(".") and exe_base.endswith("-wrapped"):
            return exe_base[1:-8]
        if name.startswith("." + exe_base) or name == ("." + exe_base)[:_COMM_MAX_LEN]:
            return exe_base
        clean = name[1:]
        base_suffix = "-wrapped"
        for i in range(len(base_suffix), 1, -1):
            if clean.endswith(base_suffix[:i]):
                clean = clean[:-i]
                break
        if clean:
            return clean
    return name


def proc_name_for_pid(pid: int) -> str:
    """Returns the cleaned process name for ``pid`` from ``/proc``, or ``""``."""
    if pid <= 0:
        return ""
    try:
        with open(f"/proc/{pid}/comm", "r", encoding="utf-8", errors="ignore") as f:
            comm = f.read().strip()
        exe = ""
        try:
            exe = os.readlink(f"/proc/{pid}/exe")
        except OSError:
            pass
        return clean_proc_name(comm, exe)
    except OSError:
        return ""


class DesktopAppIndex:
    """Indexes ``.desktop`` files for display-name and icon-name lookup."""

    def __init__(self):
        self.icon_cache: dict[str, str] = {}
        self.desktop_apps: dict[str, dict[str, str]] = {}

    def scan(self) -> None:
        """Scans standard XDG application directories and builds lookup maps."""
        self.icon_cache.clear()
        self.desktop_apps.clear()

        search_paths = [
            "/usr/share/applications",
            os.path.expanduser("~/.local/share/applications"),
            "/var/lib/flatpak/exports/share/applications",
            os.path.expanduser("~/.local/share/flatpak/exports/share/applications"),
            "/var/lib/snapd/desktop/applications",
        ]
        for directory in os.environ.get("XDG_DATA_DIRS", "").split(":"):
            if directory:
                p = os.path.join(directory, "applications")
                if p not in search_paths:
                    search_paths.append(p)

        visible_entries = []
        hidden_entries = []

        for folder in search_paths:
            if not os.path.isdir(folder):
                continue
            try:
                filenames = sorted(os.listdir(folder))
            except OSError:
                continue
            for filename in filenames:
                if not filename.endswith(".desktop"):
                    continue
                parsed = self._parse_desktop_file(os.path.join(folder, filename), filename[:-8])
                if parsed is None:
                    continue
                is_hidden, keys, name, icon = parsed
                (hidden_entries if is_hidden else visible_entries).append((keys, name, icon))

        # Index user-visible desktop entries before NoDisplay/Hidden helpers (e.g. *-url-handler).
        for keys, name, icon in visible_entries + hidden_entries:
            for k in keys:
                if icon and k not in self.icon_cache:
                    self.icon_cache[k] = icon
                if k not in self.desktop_apps:
                    self.desktop_apps[k] = {"name": name, "icon": icon}

        for k, v in _FALLBACK_ICONS.items():
            if k not in self.icon_cache:
                self.icon_cache[k] = v

    @staticmethod
    def _parse_desktop_file(path: str, desktop_id: str) -> tuple[bool, list[str], str, str] | None:
        try:
            with open(path, "r", encoding="utf-8") as df:
                content = df.read()
        except (OSError, UnicodeDecodeError):
            return None

        # Restrict metadata extraction to the primary [Desktop Entry] section.
        entry_section = content.split("\n[", 1)[0]

        icon_match = re.search(r"^Icon=([^\n]+)", entry_section, re.MULTILINE)
        icon = icon_match.group(1).strip() if icon_match else ""
        name_match = re.search(r"^Name=([^\n]+)", entry_section, re.MULTILINE)
        name = name_match.group(1).strip() if name_match else desktop_id

        keys = [desktop_id.lower()]
        if "." in desktop_id:
            keys.append(desktop_id.split(".")[-1].lower())
        if name:
            keys.append(name.lower())

        wmclass_match = re.search(r"^StartupWMClass=([^\n]+)", entry_section, re.MULTILINE)
        if wmclass_match:
            wmclass = wmclass_match.group(1).strip().lower()
            if wmclass:
                keys.append(wmclass)

        exec_match = re.search(r"^Exec=([^\n]+)", entry_section, re.MULTILINE)
        if exec_match:
            exec_line = exec_match.group(1).strip()
            if exec_line.startswith('"'):
                binary_path = exec_line[1:].split('"')[0]
            else:
                binary_path = exec_line.split(" ")[0]
            binary_name = os.path.basename(binary_path).lower()
            if binary_name:
                keys.append(binary_name)
            if binary_name == "flatpak" and " --command=" in exec_line:
                cmd_match = re.search(r"--command=([^ ]+)", exec_line)
                if cmd_match:
                    keys.append(cmd_match.group(1).lower())

        is_hidden = bool(
            re.search(
                r"^(?:NoDisplay|Hidden)=true\s*$",
                entry_section,
                re.MULTILINE | re.IGNORECASE,
            )
        )
        return is_hidden, keys, name, icon

    def lookup(self, app_id: str) -> dict[str, str] | None:
        """Returns ``{"name": ..., "icon": ...}`` for ``app_id``, or None."""
        app_lower = app_id.lower()
        if app_lower in self.desktop_apps:
            return self.desktop_apps[app_lower]
        basename = os.path.basename(app_id).lower()
        if basename in self.desktop_apps:
            return self.desktop_apps[basename]
        if "." in basename:
            short = basename.split(".")[-1]
            if short in self.desktop_apps:
                return self.desktop_apps[short]
        return None

    def display_name(self, app_id: str) -> str:
        """Returns human-readable app name for ``app_id``, falling back to ``app_id``."""
        info = self.lookup(app_id)
        return info.get("name", app_id) if info else app_id

    def resolve_icon_name(self, app_id: str) -> str:
        """Returns themed icon name or file path for ``app_id``, or ``""``."""
        app_lower = app_id.lower()
        icon_name = self.icon_cache.get(app_lower, "")
        if not icon_name:
            basename = os.path.basename(app_id).lower()
            icon_name = self.icon_cache.get(basename, "")
            if not icon_name and "." in basename:
                icon_name = self.icon_cache.get(basename.split(".")[-1], "")
        if not icon_name:
            icon_name = app_lower
        if icon_name == "application-x-executable":
            return ""
        return icon_name


def _detect_hyprland(register) -> None:
    if not os.environ.get("HYPRLAND_INSTANCE_SIGNATURE"):
        return
    try:
        res = subprocess.run(
            ["hyprctl", "clients", "-j"],
            capture_output=True,
            text=True,
            timeout=_COMPOSITOR_TIMEOUT_S,
            check=False,
        )
        if res.returncode == 0 and res.stdout.strip():
            for client in json.loads(res.stdout):
                pid = int(client.get("pid") or 0)
                proc_name = proc_name_for_pid(pid)
                win_class = client.get("initialClass") or client.get("class") or ""
                register(proc_name or win_class)
    except (OSError, subprocess.SubprocessError, ValueError):
        pass


def _detect_sway(register) -> None:
    if not os.environ.get("SWAYSOCK"):
        return
    try:
        res = subprocess.run(
            ["swaymsg", "-t", "get_tree"],
            capture_output=True,
            text=True,
            timeout=_COMPOSITOR_TIMEOUT_S,
            check=False,
        )
        if res.returncode == 0 and res.stdout.strip():

            def walk_sway(node):
                if not isinstance(node, dict):
                    return
                pid = int(node.get("pid") or 0)
                app_id = node.get("app_id") or (node.get("window_properties") or {}).get(
                    "class", ""
                )
                if pid > 0 or app_id:
                    proc_name = proc_name_for_pid(pid)
                    if proc_name or app_id:
                        register(proc_name or app_id)
                for child in node.get("nodes", []) + node.get("floating_nodes", []):
                    walk_sway(child)

            walk_sway(json.loads(res.stdout))
    except (OSError, subprocess.SubprocessError, ValueError):
        pass


def _detect_x11(register) -> None:
    if not os.environ.get("DISPLAY"):
        return
    try:
        res = subprocess.run(
            ["xprop", "-root", "_NET_CLIENT_LIST"],
            capture_output=True,
            text=True,
            timeout=_COMPOSITOR_TIMEOUT_S,
            check=False,
        )
        if res.returncode != 0:
            return
        wids = re.findall(r"0x[0-9a-fA-F]+", res.stdout)
        for wid in wids[:_MAX_X11_WINDOWS]:
            prop = subprocess.run(
                ["xprop", "-id", wid, "_NET_WM_PID", "WM_CLASS"],
                capture_output=True,
                text=True,
                timeout=_XPROP_TIMEOUT_S,
                check=False,
            )
            if prop.returncode != 0:
                continue
            pid_match = re.search(r"_NET_WM_PID\(CARDINAL\)\s*=\s*(\d+)", prop.stdout)
            proc_name = proc_name_for_pid(int(pid_match.group(1))) if pid_match else ""
            cls_matches = re.findall(r'"([^"]+)"', prop.stdout)
            win_class = cls_matches[-1] if cls_matches else ""
            if proc_name or win_class:
                register(proc_name or win_class)
    except (OSError, subprocess.SubprocessError, ValueError):
        pass


def _detect_proc(register, index: DesktopAppIndex) -> None:
    has_wayland_compositor = bool(
        os.environ.get("HYPRLAND_INSTANCE_SIGNATURE") or os.environ.get("SWAYSOCK")
    )
    try:
        current_uid = os.getuid()
        for pid_dir in os.listdir("/proc"):
            if not pid_dir.isdigit():
                continue
            pid = int(pid_dir)
            try:
                if os.stat(f"/proc/{pid}").st_uid != current_uid:
                    continue
                with open(f"/proc/{pid}/comm", "r", encoding="utf-8", errors="ignore") as f:
                    comm = f.read().strip()
                with open(f"/proc/{pid}/cmdline", "r", encoding="utf-8", errors="ignore") as f:
                    cmdline = f.read().replace("\x00", " ").strip()
                if not cmdline:
                    continue
                exe = os.readlink(f"/proc/{pid}/exe")
            except (OSError, ValueError):
                continue

            if any(exe.startswith(p) for p in _EXCLUDED_PROC_PREFIXES):
                continue

            name = clean_proc_name(comm, exe)
            basename = os.path.basename(exe).lower()
            if any(p in name.lower() or p in basename for p in _BG_PROCESS_PATTERNS):
                continue
            if ("main.py" in cmdline or "settings-gui" in cmdline) and "python" in exe:
                continue
            if basename.startswith("python") and len(cmdline.split()) < 2:
                continue
            # Include /proc hits that match a desktop app if a compositor was queried,
            # or include all filtered user apps as fallback when no compositor ran.
            if name and (not has_wayland_compositor or index.lookup(name) is not None):
                register(name)
    except OSError:
        pass


def detect_running_apps(index: DesktopAppIndex) -> list[dict[str, str]]:
    """Detects running applications via Hyprland, Sway, X11, and ``/proc``."""
    found: dict[str, dict[str, str]] = {}

    def register(app_id: str):
        cleaned = (app_id or "").strip()
        if not cleaned:
            return
        key = cleaned.lower()
        if key not in found:
            found[key] = {
                "app_id": cleaned,
                "name": index.display_name(cleaned),
            }

    _detect_hyprland(register)
    _detect_sway(register)
    if not found:
        _detect_x11(register)
    _detect_proc(register, index)

    return sorted(found.values(), key=lambda x: x["name"].lower())
