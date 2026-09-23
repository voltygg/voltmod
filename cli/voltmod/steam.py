import re
from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.server.cs2_server import GAMEINFO

CS2_APP = "730"

# Searched in order when no client path is given.
_STEAM_ROOTS = (
    "C:/Program Files (x86)/Steam",
    "C:/Program Files/Steam",
    "~/.steam/steam",
    "~/.local/share/Steam",
)
_CS2_IN_STEAM_LIBRARY = "steamapps/common/Counter-Strike Global Offensive"
_LIBRARY_PATH = re.compile(r'"path"\s+"([^"]+)"')


def is_client(root: Path) -> bool:
    return (root / GAMEINFO).is_file()


def find_client(client_path: Path | None) -> Path:
    """The CS2 client at `client_path`, or the first one in any Steam library."""
    if client_path:
        root = client_path.expanduser()
        if not is_client(root):
            raise VoltmodError(f"no CS2 client at {root}\nExpected {root / GAMEINFO}")
        return root

    for steam in _STEAM_ROOTS:
        for library in _steam_libraries(Path(steam).expanduser()):
            client = library / _CS2_IN_STEAM_LIBRARY
            if is_client(client):
                return client

    raise VoltmodError("no CS2 client found; set CS2_CLIENT_PATH in .env or pass --client")


def _steam_libraries(steam: Path) -> list[Path]:
    """Every Steam library on this machine, so a client on a second drive is still found."""
    manifest = steam / "steamapps/libraryfolders.vdf"
    if not manifest.is_file():
        return [steam]
    text = manifest.read_text(encoding="utf-8", errors="replace")
    return [steam, *(Path(path.replace("\\\\", "/")) for path in _LIBRARY_PATH.findall(text))]
