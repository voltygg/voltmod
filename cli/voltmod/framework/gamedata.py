"""Checking gamedata patterns against the game binaries, and repairing moved struct offsets."""

import json
import re
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError
from voltmod.framework.paths import GAMEDATA_FILE, SCHEMA_BASELINES
from voltmod.platforms import Platform
from voltmod.server.cs2_server import CSGO_DIR, Cs2Server

PATTERN_SECTIONS = ("functions", "globals")

# Four literal bytes reading as a little-endian integer this small may be a struct offset.
MAX_OFFSET = 0xFFFF

_LINE_COMMENT = re.compile(r"^\s*//.*$", re.MULTILINE)


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


def game_libraries(platform: Platform) -> dict[str, str]:
    """Each gamedata module's binary, relative to the install root."""
    prefix = "lib" if platform is Platform.LINUX else ""
    suffix = platform.library_suffix
    return {
        "server": f"{CSGO_DIR}/bin/{platform.bin_dir}/{prefix}server{suffix}",
        "engine2": f"game/bin/{platform.bin_dir}/{prefix}engine2{suffix}",
    }


def detect_platform(game_dir: Path) -> Platform:
    for platform in Platform:
        if any((game_dir / path).is_file() for path in game_libraries(platform).values()):
            return platform
    raise VoltmodError(f"no CS2 server or engine2 binary under {game_dir}")


def pattern_regex(pattern: str) -> re.Pattern[bytes]:
    """A gamedata byte pattern as a regex; `?` and `??` each match one byte."""
    parts = [
        b"." if token in ("?", "??") else re.escape(bytes([int(token, 16)]))
        for token in pattern.split()
    ]
    return re.compile(b"".join(parts), re.DOTALL)


def module_path(game_dir: Path, platform: Platform, module: str) -> Path:
    relative = game_libraries(platform).get(module)
    if relative is None:
        raise VoltmodError(f"unknown gamedata module '{module}'")
    path = game_dir / relative
    # Deployment images may flatten Linux binaries into one directory.
    if not path.is_file():
        path = game_dir / Path(relative).name
    if not path.is_file():
        raise VoltmodError(f"no {module} binary at {game_dir / relative}")
    return path


@dataclass(frozen=True, slots=True)
class GamedataCheck:
    text: str  # the file as read, which repairs patch in place
    platform: Platform
    game_build: str
    results: list[PatternResult]


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


def read_gamedata(root: Path) -> str:
    path = root / GAMEDATA_FILE
    if not path.is_file():
        raise VoltmodError(f"no {GAMEDATA_FILE} in {root}; run this from the framework checkout")
    return path.read_text(encoding="utf-8")


def parse_gamedata(text: str) -> dict[str, Any]:
    """The document as data; comments are dropped, and never written back."""
    return json.loads(_LINE_COMMENT.sub("", text))


def check_patterns(
    gamedata: dict[str, Any], binaries: GameBinaries, schema: dict[str, Any]
) -> list[PatternResult]:
    """Check this platform's patterns, repairing those one moved struct offset explains."""
    results = []
    for section in PATTERN_SECTIONS:
        for key, entry in sorted(gamedata.get(section, {}).items()):
            column = entry.get(binaries.platform)
            if not column:
                continue
            pattern = column["pattern"] if isinstance(column, dict) else column
            module = entry.get("module", "server")
            results.append(check_pattern(binaries, section, key, module, pattern, schema))
    return results


def check_pattern(
    binaries: GameBinaries,
    section: str,
    key: str,
    module: str,
    pattern: str,
    schema: dict[str, Any],
) -> PatternResult:
    """Whether one pattern holds, matches twice, can be repaired, or is broken."""
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
    """Wildcard the one struct offset that restores a unique match; None when zero or several do."""
    # Widen only the offset. Never search for a new function match.
    tokens = pattern.split()
    accepted = []
    for index, old_offset in _offset_candidates(tokens):
        widened = tokens.copy()
        widened[index : index + 4] = ["?"] * 4
        candidate = " ".join(widened)
        matches = binaries.find(module, candidate)
        if len(matches) == 1:
            new_offset = int.from_bytes(binaries.read(module, matches[0] + index, 4), "little")
            accepted.append(Repair(candidate, index, old_offset, new_offset))
    return accepted[0] if len(accepted) == 1 else None


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
    """Patch each repaired pattern into `text` and write it back as the gamedata file."""
    for result in repaired:
        text = replace_pattern(text, result.key, result.old_pattern, result.new_pattern)
    (root / GAMEDATA_FILE).write_text(text, encoding="utf-8", newline="\n")


def _offset_candidates(tokens: list[str]) -> list[tuple[int, int]]:
    """Every (index, value) where four literal bytes read as a plausible struct offset."""
    found = []
    for index in range(len(tokens) - 3):
        window = tokens[index : index + 4]
        if any(token in ("?", "??") for token in window):
            continue
        value = int.from_bytes(bytes(int(token, 16) for token in window), "little")
        if 0 < value <= MAX_OFFSET:
            found.append((index, value))
    return found
