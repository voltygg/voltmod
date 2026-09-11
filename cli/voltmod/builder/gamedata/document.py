"""Read gamedata.jsonc and replace one pattern without changing other text."""

import json
import re
from pathlib import Path
from typing import Any

from voltmod.tools import die

_COMMENT = re.compile(r"^\s*//.*$", re.MULTILINE)


def parse(text: str) -> dict[str, Any]:
    """Parse the document as data; comments are discarded and never written back."""
    return json.loads(_COMMENT.sub("", text))


def read(path: Path) -> tuple[str, dict[str, Any]]:
    if not path.is_file():
        die(f"no gamedata at {path}")
    text = path.read_text(encoding="utf-8")
    return text, parse(text)


def set_pattern(text: str, key: str, old: str, new: str) -> str:
    """Replace one byte pattern while preserving comments and layout."""
    at = text.find(f'"{key}"')
    if at < 0:
        die(f"gamedata has no entry named {key}")
    quoted = f'"{old}"'
    if text.count(quoted) != 1:
        die(f"the pattern for {key} is not unique in the file; edit it by hand")
    return text.replace(quoted, f'"{new}"')


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")
