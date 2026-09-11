"""Match committed signatures and repair only explained displacement drift.

Patterns that match once remain unchanged. Repair widens only compiler-baked struct displacements;
it never searches for a function, so each binding remains rooted in a human-verified signature.
"""

from dataclasses import dataclass
from typing import Any

from .image import Modules

#: A displacement worth suspecting: four literal bytes reading as a small little-endian integer.
MAX_DISPLACEMENT = 0xFFFF


@dataclass(frozen=True)
class Finding:
    """One signature's verdict and any replacement pattern."""

    key: str
    status: str  # holds | repaired | ambiguous | broken
    detail: str = ""
    old: str = ""
    new: str = ""


def _displacements(tokens: list[str]) -> list[tuple[int, int]]:
    """Every (index, value) where four literal bytes read as a plausible struct offset."""
    found = []
    for i in range(len(tokens) - 3):
        window = tokens[i : i + 4]
        if any(token in ("?", "??") for token in window):
            continue
        value = int.from_bytes(bytes(int(token, 16) for token in window), "little")
        if 0 < value <= MAX_DISPLACEMENT:
            found.append((i, value))
    return found


def repair(modules: Modules, library: str, pattern: str) -> tuple[str, int, int, int] | None:
    """Widen one displacement if it restores a unique match; refuse ambiguity."""
    tokens = pattern.split()
    accepted = []
    for index, old in _displacements(tokens):
        widened = tokens.copy()
        widened[index : index + 4] = ["?"] * 4
        candidate = " ".join(widened)
        hits = modules.find(library, candidate)
        if len(hits) == 1:
            new = int.from_bytes(modules.read(library, hits[0] + index, 4), "little")
            accepted.append((candidate, index, old, new))
    return accepted[0] if len(accepted) == 1 else None


def fields_at(schema: dict[str, Any], offset: int) -> list[str]:
    """Return schema fields at `offset` to identify a repaired displacement."""
    return [
        f"{name}::{field['name']}"
        for name, info in sorted(schema.get("classes", {}).items())
        for field in info.get("fields", [])
        if field.get("offset") == offset
    ]


def _explain(index: int, old: int, new: int, schema: dict[str, Any]) -> str:
    detail = f"bytes {index}-{index + 3}: {old} -> {new}, wildcarded"
    named = fields_at(schema, new)
    return f"{detail}\n{new} is {', '.join(named[:2])}" if named else detail


def run(document: dict[str, Any], modules: Modules, schema: dict[str, Any]) -> list[Finding]:
    """Check selected-platform signatures and repair those with one explanation."""
    findings = []
    for key, entry in sorted(document.get("signatures", {}).items()):
        column = entry.get(modules.platform)
        if not column:
            continue

        library = entry.get("library", "server")
        pattern = column["pattern"]
        hits = modules.find(library, pattern)
        if len(hits) == 1:
            findings.append(Finding(key, "holds"))
            continue
        if len(hits) > 1:
            findings.append(Finding(key, "ambiguous", f"{len(hits)} matches"))
            continue

        repaired = repair(modules, library, pattern)
        if repaired is None:
            findings.append(
                Finding(key, "broken", "no match, and no single displacement explains it")
            )
            continue

        candidate, index, old, new = repaired
        detail = _explain(index, old, new, schema)
        findings.append(Finding(key, "repaired", detail, pattern, candidate))
    return findings
