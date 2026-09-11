"""Map CS2 platform libraries and search their bytes."""

import re
from pathlib import Path

from voltmod.tools import die

#: Library paths relative to a CS2 install, by platform.
MODULES = {
    "windows": {
        "server": "game/csgo/bin/win64/server.dll",
        "engine2": "game/bin/win64/engine2.dll",
    },
    "linux": {
        "server": "game/csgo/bin/linuxsteamrt64/libserver.so",
        "engine2": "game/bin/linuxsteamrt64/libengine2.so",
    },
}


def detect_platform(game_dir: Path) -> str:
    """Return the platform whose binaries `game_dir` contains."""
    for platform, modules in MODULES.items():
        if any((game_dir / rel).is_file() for rel in modules.values()):
            return platform
    die(f"no CS2 server or engine2 binary under {game_dir}")


def pattern_regex(pattern: str) -> re.Pattern[bytes]:
    """Convert a gamedata pattern to a regex; `?` and `??` each match one byte."""
    parts = [
        b"." if token in ("?", "??") else re.escape(bytes([int(token, 16)]))
        for token in pattern.split()
    ]
    return re.compile(b"".join(parts), re.DOTALL)


class Modules:
    """Platform binaries loaded once and searched as files.

    File scans approximate the runtime's mapped-image scan. Extra file bytes can make a pattern
    ambiguous here even when it is unique in memory, so ambiguity is reported rather than resolved.
    """

    def __init__(self, game_dir: Path, platform: str) -> None:
        self.game_dir = game_dir
        self.platform = platform
        self._data: dict[str, bytes] = {}

    def data(self, library: str) -> bytes:
        if library not in self._data:
            relative = MODULES[self.platform].get(library)
            if relative is None:
                die(f"unknown gamedata library '{library}'")
            path = self.game_dir / relative
            # Linux binaries copied from the deploy image may be supplied in a flat directory.
            if not path.is_file():
                path = self.game_dir / Path(relative).name
            if not path.is_file():
                die(f"no {library} binary at {self.game_dir / relative}")
            self._data[library] = path.read_bytes()
        return self._data[library]

    def find(self, library: str, pattern: str) -> list[int]:
        """Return every offset where `pattern` matches."""
        return [m.start() for m in pattern_regex(pattern).finditer(self.data(library))]

    def read(self, library: str, at: int, length: int) -> bytes:
        return self.data(library)[at : at + length]
