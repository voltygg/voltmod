"""Check committed gamedata against shipped binaries and repair only explained drift.

`image` loads searchable binaries, `document` preserves JSONC layout, `resolve` matches and
repairs patterns, and `command` exposes the CLI.
"""

from .command import app
from .image import Modules, detect_platform, pattern_regex
from .resolve import Finding, fields_at, repair, run

__all__ = [
    "Finding",
    "Modules",
    "app",
    "detect_platform",
    "fields_at",
    "pattern_regex",
    "repair",
    "run",
]
