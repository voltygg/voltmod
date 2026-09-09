"""The theme: the colours and timings a screen template spells as `{{surface.base}}`."""

from pathlib import Path
from typing import Any

import yaml

from voltmod.tools import die

THEME_FILE = "panorama/theme.yaml"


def load(root: Path, kit_root: Path) -> dict[str, Any]:
    """The framework's theme with the project's own merged over it."""
    return _merge(_read(kit_root / THEME_FILE), _read(root / THEME_FILE))


def _read(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    values = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    if not isinstance(values, dict):
        die(f"{path}: a theme is a mapping of names to values")
    return values


def _merge(base: dict[str, Any], over: dict[str, Any]) -> dict[str, Any]:
    """Groups merge key by key so an override keeps the rest; a list is replaced whole."""
    merged = dict(base)
    for key, value in over.items():
        current = merged.get(key)
        if isinstance(current, dict) and isinstance(value, dict):
            merged[key] = _merge(current, value)
        else:
            merged[key] = value
    return merged
