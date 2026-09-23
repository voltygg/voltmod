import json
from collections.abc import Iterator
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError
from voltmod.framework.gamedata import (
    detect_platform,
    is_wildcard,
    module_path,
    parse_gamedata,
    pattern_regex,
    read_gamedata,
)
from voltmod.framework.paths import GAMEDATA_FILE, SCHEMA_BASELINES
from voltmod.platforms import Platform
from voltmod.server.cs2_server import Cs2Server

PATTERN_SECTIONS = ("functions", "globals")

# Four literal bytes reading as a little-endian integer this small may be a struct offset.
MAX_OFFSET = 0xFFFF


class PatternStatus(StrEnum):
    UNIQUE = "unique"
    REPAIRED = "repaired"
    AMBIGUOUS = "ambiguous"
    MISSING = "missing"


@dataclass(frozen=True, slots=True)
class PatternResult:
    section: str
    key: str
    status: PatternStatus
    detail: str = ""
    old_pattern: str = ""
    new_pattern: str = ""


@dataclass(frozen=True, slots=True)
class Repair:
    """A pattern with one struct offset wildcarded, and the offset it now reads."""

    pattern: str
    index: int  # the offset's first byte in the pattern
    old_offset: int
    new_offset: int


@dataclass(frozen=True, slots=True)
class GamedataCheck:
    text: str  # the file as read, which repairs patch in place
    platform: Platform
    game_build: str
    results: list[PatternResult]


class GameBinaries:
    """Game modules read once and searched as files.

    A file holds bytes the loaded module does not, so a pattern unique at runtime can look
    ambiguous here; ambiguity is reported, never resolved.
    """

    def __init__(self, game_dir: Path, platform: Platform) -> None:
        self.game_dir = game_dir
        self.platform = platform
        self._contents: dict[str, bytes] = {}

    def contents(self, module: str) -> bytes:
        if module not in self._contents:
            path = module_path(self.game_dir, self.platform, module)
            self._contents[module] = path.read_bytes()
        return self._contents[module]

    def find(self, module: str, pattern: str) -> list[int]:
        return [match.start() for match in pattern_regex(pattern).finditer(self.contents(module))]

    def read(self, module: str, offset: int, length: int) -> bytes:
        return self.contents(module)[offset : offset + length]


def check_gamedata(root: Path, game_dir: Path | None, platform: Platform | None) -> GamedataCheck:
    """Every committed pattern in `root` checked against the game at `game_dir`."""
    if game_dir is None:
        raise VoltmodError("no game directory; set CS2_SERVER_PATH in .env or pass --game-dir")
    game = game_dir.expanduser()
    if not game.is_dir():
        raise VoltmodError(f"no game directory at {game}")

    text = read_gamedata(root)
    binaries = GameBinaries(game, platform or detect_platform(game))
    baseline = root / SCHEMA_BASELINES[binaries.platform]
    schema = json.loads(baseline.read_text(encoding="utf-8")) if baseline.is_file() else {}

    results = check_patterns(parse_gamedata(text), binaries, schema)
    return GamedataCheck(text, binaries.platform, Cs2Server(game).build, results)


def check_patterns(
    gamedata: dict[str, Any], binaries: GameBinaries, schema: dict[str, Any]
) -> list[PatternResult]:
    """Check this platform's patterns, repairing those one moved struct offset explains."""
    return [
        check_pattern(binaries, section, key, module, pattern, schema)
        for section, key, module, pattern in _patterns(gamedata, binaries.platform)
    ]


def check_pattern(
    binaries: GameBinaries,
    section: str,
    key: str,
    module: str,
    pattern: str,
    schema: dict[str, Any],
) -> PatternResult:
    """Whether one pattern is unique, ambiguous, repairable from a moved offset, or missing."""
    matches = binaries.find(module, pattern)
    if len(matches) == 1:
        return PatternResult(section, key, PatternStatus.UNIQUE)
    if len(matches) > 1:
        return PatternResult(section, key, PatternStatus.AMBIGUOUS, f"{len(matches)} matches")

    repaired = repair_pattern(binaries, module, pattern)
    if repaired is None:
        detail = "no match, and no single moved offset explains it"
        return PatternResult(section, key, PatternStatus.MISSING, detail)

    start, end = repaired.index, repaired.index + 3
    detail = f"bytes {start}-{end}: {repaired.old_offset} -> {repaired.new_offset}, wildcarded"
    if fields := schema_fields_at(schema, repaired.new_offset):
        detail += f"\n{repaired.new_offset} is {', '.join(fields[:2])}"
    return PatternResult(section, key, PatternStatus.REPAIRED, detail, pattern, repaired.pattern)


def repair_pattern(binaries: GameBinaries, module: str, pattern: str) -> Repair | None:
    """Wildcard the one struct offset that restores a unique match; None when zero or several do.

    Only the offset widens: this never searches for a different function.
    """
    tokens = pattern.split()
    repairs = []
    for index, old_offset in _offset_candidates(tokens):
        candidate = " ".join(tokens[:index] + ["?"] * 4 + tokens[index + 4 :])
        matches = binaries.find(module, candidate)
        if len(matches) == 1:
            new_offset = int.from_bytes(binaries.read(module, matches[0] + index, 4), "little")
            repairs.append(Repair(candidate, index, old_offset, new_offset))
    return repairs[0] if len(repairs) == 1 else None


def schema_fields_at(schema: dict[str, Any], offset: int) -> list[str]:
    """Schema fields at `offset`, to name what a repaired offset points at."""
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


def write_repairs(root: Path, text: str, repaired: list[PatternResult]) -> None:
    for result in repaired:
        text = replace_pattern(text, result.key, result.old_pattern, result.new_pattern)
    (root / GAMEDATA_FILE).write_text(text, encoding="utf-8", newline="\n")


def _patterns(gamedata: dict[str, Any], platform: Platform) -> Iterator[tuple[str, str, str, str]]:
    """(section, key, module, pattern) for each entry with a pattern for `platform`."""
    for section in PATTERN_SECTIONS:
        for key, entry in sorted(gamedata.get(section, {}).items()):
            if column := entry.get(platform):
                pattern = column["pattern"] if isinstance(column, dict) else column
                yield section, key, entry.get("module", "server"), pattern


def _offset_candidates(tokens: list[str]) -> list[tuple[int, int]]:
    """Every (index, value) where four literal bytes read as a plausible struct offset."""
    windows = [(index, tokens[index : index + 4]) for index in range(len(tokens) - 3)]
    values = [
        (index, int.from_bytes(bytes(int(token, 16) for token in window), "little"))
        for index, window in windows
        if not any(is_wildcard(token) for token in window)
    ]
    return [(index, value) for index, value in values if 0 < value <= MAX_OFFSET]
