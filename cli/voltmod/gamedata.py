"""Checking gamedata signatures against the game binaries, and repairing moved struct offsets."""

import json
import re
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Any

from voltmod.cs2_install import GAME_LIBRARIES, game_build
from voltmod.errors import VoltmodError

GAMEDATA_FILE = Path("gamedata/gamedata.jsonc")
SCHEMA_BASELINE = Path("schema/server.json")

# Four literal bytes reading as a little-endian integer this small may be a struct offset.
MAX_DISPLACEMENT = 0xFFFF

_LINE_COMMENT = re.compile(r"^\s*//.*$", re.MULTILINE)


class SignatureStatus(StrEnum):
    HOLDS = "holds"
    REPAIRED = "repaired"
    AMBIGUOUS = "ambiguous"
    BROKEN = "broken"


@dataclass(frozen=True, slots=True)
class SignatureResult:
    key: str
    status: SignatureStatus
    detail: str = ""
    old_pattern: str = ""
    new_pattern: str = ""


class GameBinaries:
    """Game libraries read once and searched as files.

    A file holds bytes the loaded image does not, so a pattern unique at runtime can look
    ambiguous here; ambiguity is reported, never resolved.
    """

    def __init__(self, game_dir: Path, platform: str) -> None:
        self.game_dir = game_dir
        self.platform = platform
        self._contents: dict[str, bytes] = {}

    def contents(self, library: str) -> bytes:
        if library not in self._contents:
            relative = GAME_LIBRARIES[self.platform].get(library)
            if relative is None:
                raise VoltmodError(f"unknown gamedata library '{library}'")
            path = self.game_dir / relative
            # Linux binaries copied out of the deploy image may sit in one flat folder.
            if not path.is_file():
                path = self.game_dir / Path(relative).name
            if not path.is_file():
                raise VoltmodError(f"no {library} binary at {self.game_dir / relative}")
            self._contents[library] = path.read_bytes()
        return self._contents[library]

    def find(self, library: str, pattern: str) -> list[int]:
        return [match.start() for match in pattern_regex(pattern).finditer(self.contents(library))]

    def read(self, library: str, offset: int, length: int) -> bytes:
        return self.contents(library)[offset : offset + length]


def detect_platform(game_dir: Path) -> str:
    for platform, libraries in GAME_LIBRARIES.items():
        if any((game_dir / path).is_file() for path in libraries.values()):
            return platform
    raise VoltmodError(f"no CS2 server or engine2 binary under {game_dir}")


def pattern_regex(pattern: str) -> re.Pattern[bytes]:
    """A gamedata byte pattern as a regex; `?` and `??` each match one byte."""
    parts = [
        b"." if token in ("?", "??") else re.escape(bytes([int(token, 16)]))
        for token in pattern.split()
    ]
    return re.compile(b"".join(parts), re.DOTALL)


def check_gamedata(root: Path, game_dir: str, platform: str) -> tuple[str, list[SignatureResult]]:
    """The gamedata text in @p root, and every signature checked against the game at @p game_dir."""
    if not game_dir:
        raise VoltmodError("no game directory; set CS2_SERVER_PATH in .env or pass --game-dir")
    game = Path(game_dir).expanduser()
    if not game.is_dir():
        raise VoltmodError(f"no game directory at {game}")

    path = root / GAMEDATA_FILE
    if not path.is_file():
        raise VoltmodError(f"no {GAMEDATA_FILE} in {root}; run this from the framework checkout")
    text = path.read_text(encoding="utf-8")

    binaries = GameBinaries(game, platform or detect_platform(game))
    baseline = root / SCHEMA_BASELINE
    schema = json.loads(baseline.read_text(encoding="utf-8")) if baseline.is_file() else {}

    print(f"==> gamedata {binaries.platform} (game build {game_build(game)})")
    return text, check_signatures(parse_gamedata(text), binaries, schema)


def parse_gamedata(text: str) -> dict[str, Any]:
    """The document as data; comments are dropped, and never written back."""
    return json.loads(_LINE_COMMENT.sub("", text))


def check_signatures(
    gamedata: dict[str, Any], binaries: GameBinaries, schema: dict[str, Any]
) -> list[SignatureResult]:
    """Check this platform's signatures, repairing those one moved displacement explains."""
    results = []
    for key, entry in sorted(gamedata.get("signatures", {}).items()):
        column = entry.get(binaries.platform)
        if not column:
            continue

        library = entry.get("library", "server")
        pattern = column["pattern"]
        hits = binaries.find(library, pattern)
        if len(hits) == 1:
            results.append(SignatureResult(key, SignatureStatus.HOLDS))
            continue
        if len(hits) > 1:
            results.append(SignatureResult(key, SignatureStatus.AMBIGUOUS, f"{len(hits)} matches"))
            continue

        repaired = repair_signature(binaries, library, pattern)
        if repaired is None:
            detail = "no match, and no single displacement explains it"
            results.append(SignatureResult(key, SignatureStatus.BROKEN, detail))
            continue

        new_pattern, index, old_offset, new_offset = repaired
        detail = f"bytes {index}-{index + 3}: {old_offset} -> {new_offset}, wildcarded"
        if fields := schema_fields_at(schema, new_offset):
            detail += f"\n{new_offset} is {', '.join(fields[:2])}"
        results.append(
            SignatureResult(key, SignatureStatus.REPAIRED, detail, pattern, new_pattern)
        )
    return results


def repair_signature(
    binaries: GameBinaries, library: str, pattern: str
) -> tuple[str, int, int, int] | None:
    """Wildcard the one displacement that restores a unique match: (pattern, index, old, new)."""
    # Only a displacement is widened; a function is never searched for anew.
    tokens = pattern.split()
    accepted = []
    for index, old_offset in _displacements(tokens):
        widened = tokens.copy()
        widened[index : index + 4] = ["?"] * 4
        candidate = " ".join(widened)
        hits = binaries.find(library, candidate)
        if len(hits) == 1:
            new_offset = int.from_bytes(binaries.read(library, hits[0] + index, 4), "little")
            accepted.append((candidate, index, old_offset, new_offset))
    return accepted[0] if len(accepted) == 1 else None


def schema_fields_at(schema: dict[str, Any], offset: int) -> list[str]:
    """Schema fields at @p offset, to name what a repaired displacement points at."""
    return [
        f"{name}::{field['name']}"
        for name, info in sorted(schema.get("classes", {}).items())
        for field in info.get("fields", [])
        if field.get("offset") == offset
    ]


def replace_pattern(text: str, key: str, old_pattern: str, new_pattern: str) -> str:
    """Replace one byte pattern, keeping every comment and blank line."""
    if f'"{key}"' not in text:
        raise VoltmodError(f"gamedata has no entry named {key}")
    quoted = f'"{old_pattern}"'
    if text.count(quoted) != 1:
        raise VoltmodError(f"the pattern for {key} is not unique in the file; edit it by hand")
    return text.replace(quoted, f'"{new_pattern}"')


def _displacements(tokens: list[str]) -> list[tuple[int, int]]:
    """Every (index, value) where four literal bytes read as a plausible struct offset."""
    found = []
    for index in range(len(tokens) - 3):
        window = tokens[index : index + 4]
        if any(token in ("?", "??") for token in window):
            continue
        value = int.from_bytes(bytes(int(token, 16) for token in window), "little")
        if 0 < value <= MAX_DISPLACEMENT:
            found.append((index, value))
    return found
