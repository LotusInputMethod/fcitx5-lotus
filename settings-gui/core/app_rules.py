# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-application mode rule storage, testable without Qt.

Rules map an application handle to ``{"mode", "commit", "backspace", "post"}``
and are stored via D-Bus as rows of ``{"App", "Mode", "CommitInterval",
"BackspaceInterval", "PostDelay"}`` string fields.
"""

from collections.abc import Iterable, Mapping

from core.app_catalog import parse_app_rule_item
from core.mode_types import MODE_DEFAULT

RULE_DEFAULTS = {"commit": 0, "backspace": 0, "post": 0}


def default_rule(mode: int) -> dict:
    """Builds a rule with the given mode and zero delays."""
    return {"mode": mode, **RULE_DEFAULTS}


def parse_app_rules(items: Iterable[Mapping]) -> dict[str, dict]:
    """Parses D-Bus rule rows into an app -> rule dict, skipping bad rows."""
    rules = {}
    for item in items:
        parsed = parse_app_rule_item(item)
        if parsed is None:
            print(f"Skipping malformed app rule: {item!r}")
            continue
        app, rule = parsed
        if app:
            rules[app] = rule
    return rules


def serialize_app_rules(app_rules: Mapping[str, Mapping]) -> list[dict]:
    """Serializes rules to D-Bus rows, ordered by application handle."""
    return [
        {
            "App": app,
            "Mode": str(rule["mode"]),
            "CommitInterval": str(rule.get("commit", 0)),
            "BackspaceInterval": str(rule.get("backspace", 0)),
            "PostDelay": str(rule.get("post", 0)),
        }
        for app, rule in sorted(app_rules.items())
    ]


def apply_app_mode(app_rules: dict, app: str, mode: int) -> None:
    """Applies a mode choice: MODE_DEFAULT drops the rule, others update it."""
    if mode == MODE_DEFAULT:
        app_rules.pop(app, None)
        return
    rule = app_rules.setdefault(app, default_rule(mode))
    rule["mode"] = mode


def app_matches_filter(text: str, item_text: str) -> bool:
    """Returns True when the search text matches the app id of a list row."""
    return text.lower() in item_text.split("\n")[0].lower()


def rule_delay(rule: Mapping, field: str) -> int:
    """Reads a delay field of a rule as an int."""
    return int(rule.get(field, 0))
