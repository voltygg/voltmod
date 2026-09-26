import json
import re
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError
from voltmod.framework.paths import GAMEDATA_FILE
from voltmod.platforms import Platform
from voltmod.server.cs2_server import CSGO_DIR

_LINE_COMMENT = re.compile(r"^\s*//.*$", re.MULTILINE)
_WILDCARDS = ("?", "??")


def read_gamedata(root: Path) -> str:
    path = root / GAMEDATA_FILE
    if not path.is_file():
        raise VoltmodError(f"no {GAMEDATA_FILE} in {root}; run this from the framework checkout")
    return path.read_text(encoding="utf-8")


def parse_gamedata(text: str) -> dict[str, Any]:
    """The document without its comments, which are never written back from here."""
    return json.loads(_LINE_COMMENT.sub("", text))


def is_wildcard(token: str) -> bool:
    return token in _WILDCARDS


def pattern_regex(pattern: str) -> re.Pattern[bytes]:
    """A gamedata byte pattern as a regex; `?` and `??` each match one byte."""
    parts = [
        b"." if is_wildcard(token) else re.escape(bytes([int(token, 16)]))
        for token in pattern.split()
    ]
    return re.compile(b"".join(parts), re.DOTALL)


def game_libraries(platform: Platform) -> dict[str, str]:
    """Each gamedata module's binary, relative to the install root."""
    prefix = platform.library_prefix
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


def module_path(game_dir: Path, platform: Platform, module: str) -> Path:
    relative = game_libraries(platform).get(module)
    if relative is None:
        raise VoltmodError(f"unknown gamedata module '{module}'")
    # Deployment images may flatten Linux binaries into one directory.
    for path in (game_dir / relative, game_dir / Path(relative).name):
        if path.is_file():
            return path
    raise VoltmodError(f"no {module} binary at {game_dir / relative}")
