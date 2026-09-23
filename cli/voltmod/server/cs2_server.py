import re
from dataclasses import dataclass
from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.platforms import Platform

# The game directory every addons/ path is relative to.
CSGO_DIR = "game/csgo"
STEAM_INF = f"{CSGO_DIR}/steam.inf"
GAMEINFO = f"{CSGO_DIR}/gameinfo.gi"

METAMOD_SEARCH_PATH = "csgo/addons/metamod"
_GAME_CSGO_LINE = re.compile(r"^([ \t]*)Game[ \t]+csgo[ \t]*(\r?)$", re.MULTILINE)


@dataclass(frozen=True, slots=True)
class Cs2Server:
    """A CS2 install: a dedicated server, or a folder of its files such as an archived build."""

    root: Path

    @classmethod
    def open(cls, path: Path | None) -> Cs2Server:
        """The server at `path`, which must hold game/csgo."""
        if path is None:
            raise VoltmodError("no CS2 server path; set CS2_SERVER_PATH in .env or pass --server")
        root = path.expanduser()
        if not (root / CSGO_DIR).is_dir():
            raise VoltmodError(
                f"CS2 server not found at {root / CSGO_DIR}\n"
                "Set CS2_SERVER_PATH in .env or pass --server"
            )
        return cls(root)

    @property
    def game_dir(self) -> Path:
        return self.root / CSGO_DIR

    @property
    def executable(self) -> Path | None:
        """This host's server executable, else the other platform's, else None."""
        for platform in (Platform.host(), *Platform):
            if (path := self.root / platform.server_executable).is_file():
                return path
        return None

    @property
    def build(self) -> str:
        """The build number in steam.inf, which the framework also stamps on schema dumps."""
        return self._steam_inf("ServerVersion")

    @property
    def patch_version(self) -> str:
        """The dotted version in steam.inf, which Steam's up-to-date check takes."""
        return self._steam_inf("PatchVersion")

    def has_metamod_search_path(self) -> bool:
        gameinfo = self.root / GAMEINFO
        return METAMOD_SEARCH_PATH in gameinfo.read_text(encoding="utf-8", errors="replace")

    def restore_metamod_search_path(self) -> bool:
        """Put Metamod's line back above `Game csgo`; a CS2 update drops it from gameinfo.gi."""
        gameinfo = self.root / GAMEINFO
        text = gameinfo.read_bytes().decode("utf-8")
        if METAMOD_SEARCH_PATH in text:
            return False
        replacement = rf"\1Game\t{METAMOD_SEARCH_PATH}\2\n\g<0>"
        patched, count = _GAME_CSGO_LINE.subn(replacement, text, count=1)
        if not count:
            raise VoltmodError(f"{gameinfo} has no `Game csgo` line to put Metamod above")
        gameinfo.write_bytes(patched.encode("utf-8"))
        return True

    def _steam_inf(self, key: str) -> str:
        steam_inf = self.root / STEAM_INF
        if not steam_inf.is_file():
            return "unknown"
        text = steam_inf.read_text(encoding="utf-8", errors="replace")
        found = re.search(rf"{key}=(\S+)", text)
        return found.group(1) if found else "unknown"
