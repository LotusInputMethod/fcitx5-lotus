# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""App catalog scanning for the mode manager page, testable without Qt.

Builds a robust map of app identifiers (desktop id, binary name, display
name, flatpak command) to icon names from the installed .desktop files.
"""

import os
import re
from collections.abc import Iterable, Mapping, MutableMapping

# Manual overrides for stubborn apps the desktop files do not resolve.
MANUAL_ICONS = {
    "discord": "discord",
    "upnote": "upnote",
    "antigravity": "preferences-desktop-accessibility",
}


def desktop_search_paths(environ: Mapping[str, str], home: str) -> list[str]:
    """Builds the application directory search paths, XDG dirs appended last."""
    paths = [
        "/usr/share/applications",
        os.path.join(home, ".local/share/applications"),
        "/var/lib/flatpak/exports/share/applications",
        os.path.join(home, ".local/share/flatpak/exports/share/applications"),
        "/var/lib/snapd/desktop/applications",
    ]
    for directory in environ.get("XDG_DATA_DIRS", "").split(":"):
        if directory:
            applications_path = os.path.join(directory, "applications")
            if applications_path not in paths:
                paths.append(applications_path)
    return paths


def parse_app_rule_item(item: Mapping) -> tuple[str, dict] | None:
    """Parses one D-Bus app rule row into (app, rule); None when malformed."""
    try:
        app = item.get("App", "")
        rule = {
            "mode": int(item.get("Mode", 0)),
            "commit": int(item.get("CommitInterval", 0) or 0),
            "backspace": int(item.get("BackspaceInterval", 0) or 0),
            "post": int(item.get("PostDelay", 0) or 0),
        }
    except (ValueError, TypeError, AttributeError):
        return None
    return app, rule


def parse_desktop_file(content: str, desktop_id: str) -> dict[str, str] | None:
    """Extracts the alias to icon mapping of one desktop file.

    Returns None when the file carries no Icon entry, so the caller skips it
    entirely like the page did before.
    """
    icon_match = re.search(r"^Icon=([^\n]+)", content, re.MULTILINE)
    if not icon_match:
        return None
    icon = icon_match.group(1).strip()

    aliases = {}
    # Map by desktop ID (e.g. discord)
    aliases[desktop_id.lower()] = icon
    if "." in desktop_id:  # handle com.discordapp.Discord
        aliases[desktop_id.split(".")[-1].lower()] = icon

    name_match = re.search(r"^Name=([^\n]+)", content, re.MULTILINE)
    if name_match:
        aliases[name_match.group(1).strip().lower()] = icon

    exec_match = re.search(r"^Exec=([^\n]+)", content, re.MULTILINE)
    if exec_match:
        exec_line = exec_match.group(1).strip()
        # Robustly extract binary name (handle quotes and arguments)
        if exec_line.startswith('"'):
            binary_path = exec_line[1:].split('"')[0]
        else:
            binary_path = exec_line.split(" ")[0]

        binary_name = os.path.basename(binary_path).lower()
        aliases[binary_name] = icon

        if binary_name == "flatpak" and " --command=" in exec_line:
            cmd_match = re.search(r"--command=([^ ]+)", exec_line)
            if cmd_match:
                aliases[cmd_match.group(1).lower()] = icon
    return aliases


def scan_desktop_icons(paths: Iterable[str], cache: MutableMapping[str, str]) -> None:
    """Adds the icon aliases of every readable desktop file into cache.

    Later files win over earlier ones, and unreadable files are skipped;
    only unexpected errors are logged, like the page did before.
    """
    for path in paths:
        if not os.path.isdir(path):
            continue
        for file_name in os.listdir(path):
            if not file_name.endswith(".desktop"):
                continue
            try:
                with open(os.path.join(path, file_name), "r", encoding="utf-8") as df:
                    aliases = parse_desktop_file(df.read(), file_name[:-8])
                if aliases:
                    cache.update(aliases)
            except (PermissionError, FileNotFoundError):
                continue
            except Exception as e:
                # Only log truly unexpected errors
                if not isinstance(e, (UnicodeDecodeError, re.error)):
                    print(f"Error parsing desktop file {file_name}: {e}")
                continue
    apply_manual_overrides(cache)


def apply_manual_overrides(cache: MutableMapping[str, str]) -> None:
    """Fills the manual icon overrides without touching resolved entries."""
    for key, icon in MANUAL_ICONS.items():
        if key not in cache:
            cache[key] = icon


def resolve_icon_name(app_name: str, icon_cache: Mapping[str, str]) -> str:
    """Resolves an icon name for an app handle, falling back to the handle itself."""
    icon_name = icon_cache.get(app_name.lower())

    if not icon_name:
        # Try removing extension or path if it's a full path
        basename = os.path.basename(app_name).lower()
        if "." in basename:
            basename = basename.split(".")[0]
        icon_name = icon_cache.get(basename, basename)

    return icon_name
