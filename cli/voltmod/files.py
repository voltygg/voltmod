import difflib
import json
import sys
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError


def read_json(path: Path, description: str) -> Any:
    if not path.is_file():
        raise VoltmodError(f"no {description} at {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def write_if_changed(path: Path, data: str | bytes) -> bool:
    """Write unless the bytes already match, so an unchanged file keeps its timestamp."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def write_or_check(path: Path, text: str, *, check: bool) -> bool:
    """Write `text`, or with `check` print a diff and fail when the file differs from it."""
    if not check:
        return write_if_changed(path, text)
    current = path.read_text(encoding="utf-8") if path.is_file() else ""
    if current != text:
        diff = difflib.unified_diff(
            current.splitlines(True), text.splitlines(True), f"{path} (current)", "generated"
        )
        sys.stdout.writelines(diff)
        raise VoltmodError(f"{path} is out of date; run the generator without --check")
    return False
