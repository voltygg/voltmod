"""Where a CS2 server or client install keeps the files voltmod reads and runs."""

import re
from pathlib import Path

from voltmod.errors import VoltmodError

BIN_SUBDIR = {"windows": "win64", "linux": "linuxsteamrt64"}

CS2_APP = "730"

# The game directory every addons/ path below is relative to.
CSGO_DIR = "game/csgo"
STEAM_INF = f"{CSGO_DIR}/steam.inf"
GAMEINFO = f"{CSGO_DIR}/gameinfo.gi"

SERVER_EXECUTABLES = (
    f"game/bin/{BIN_SUBDIR['windows']}/cs2.exe",
    f"game/bin/{BIN_SUBDIR['linux']}/cs2",
)
METAMOD_BINARIES = (
    f"addons/metamod/bin/{BIN_SUBDIR['windows']}/server.dll",
    f"addons/metamod/bin/{BIN_SUBDIR['linux']}/server.so",
)

# The host is the only Metamod plugin: one per server, loading modules from its plugins directory.
HOST_COMPONENT = "host"
HOST_ADDON_DIR = "addons/voltmod"
PLUGINS_DIR = f"{HOST_ADDON_DIR}/plugins"
HOST_VDF = "addons/metamod/voltmod.vdf"
HOST_GAMEDATA = f"{HOST_ADDON_DIR}/gamedata/gamedata.jsonc"
HOST_BINARIES = {
    "windows": f"{HOST_ADDON_DIR}/bin/{BIN_SUBDIR['windows']}/voltmod.dll",
    "linux": f"{HOST_ADDON_DIR}/bin/{BIN_SUBDIR['linux']}/voltmod.so",
}

# Written by a server running voltmod once a map runs.
SCHEMA_DUMP = f"{CSGO_DIR}/{HOST_ADDON_DIR}/schema/server.json"

GAME_LIBRARIES = {
    "windows": {
        "server": f"{CSGO_DIR}/bin/{BIN_SUBDIR['windows']}/server.dll",
        "engine2": f"game/bin/{BIN_SUBDIR['windows']}/engine2.dll",
    },
    "linux": {
        "server": f"{CSGO_DIR}/bin/{BIN_SUBDIR['linux']}/libserver.so",
        "engine2": f"game/bin/{BIN_SUBDIR['linux']}/libengine2.so",
    },
}

RESOURCE_COMPILER = f"game/bin/{BIN_SUBDIR['windows']}/resourcecompiler.exe"


def plugin_dir(name: str) -> str:
    """Where one plugin's files live, relative to the game directory."""
    return f"{PLUGINS_DIR}/{name}"


# Searched in order when no client path is given.
_STEAM_ROOTS = (
    "C:/Program Files (x86)/Steam",
    "C:/Program Files/Steam",
    "~/.steam/steam",
    "~/.local/share/Steam",
)
_CS2_IN_STEAM_LIBRARY = "steamapps/common/Counter-Strike Global Offensive"
_LIBRARY_PATH = re.compile(r'"path"\s+"([^"]+)"')


def find_server(server_path: str) -> Path:
    if not server_path:
        raise VoltmodError("no CS2 server path; set CS2_SERVER_PATH in .env or pass --server-path")
    root = Path(server_path).expanduser()
    if not (root / CSGO_DIR).is_dir():
        raise VoltmodError(
            f"CS2 server not found at {root / CSGO_DIR}\n"
            "Set CS2_SERVER_PATH in .env or pass --server-path"
        )
    return root


def server_executable(root: Path) -> Path | None:
    return next((root / path for path in SERVER_EXECUTABLES if (root / path).is_file()), None)


def game_build(root: Path) -> str:
    """The build number in steam.inf, which the framework also stamps on schema dumps."""
    return _steam_inf(root, "ServerVersion")


def patch_version(root: Path) -> str:
    """The dotted version in steam.inf, which Steam's up-to-date check takes."""
    return _steam_inf(root, "PatchVersion")


def _steam_inf(root: Path, key: str) -> str:
    steam_inf = root / STEAM_INF
    if not steam_inf.is_file():
        return "unknown"
    text = steam_inf.read_text(encoding="utf-8", errors="replace")
    found = re.search(rf"{key}=(\S+)", text)
    return found.group(1) if found else "unknown"


METAMOD_SEARCH_PATH = "csgo/addons/metamod"
_GAME_CSGO_LINE = re.compile(r"^([ \t]*)Game[ \t]+csgo[ \t]*(\r?)$", re.MULTILINE)


def has_metamod_search_path(root: Path) -> bool:
    return METAMOD_SEARCH_PATH in (root / GAMEINFO).read_text(encoding="utf-8", errors="replace")


def restore_metamod_search_path(root: Path) -> bool:
    """Put Metamod's line back above `Game csgo`; a CS2 update rewrites gameinfo.gi without it."""
    gameinfo = root / GAMEINFO
    text = gameinfo.read_bytes().decode("utf-8")
    if METAMOD_SEARCH_PATH in text:
        return False
    patched, count = _GAME_CSGO_LINE.subn(rf"\1Game\t{METAMOD_SEARCH_PATH}\2\n\g<0>", text, count=1)
    if not count:
        raise VoltmodError(f"{gameinfo} has no `Game csgo` line to put Metamod above")
    gameinfo.write_bytes(patched.encode("utf-8"))
    return True


def is_client(root: Path) -> bool:
    return (root / GAMEINFO).is_file()


def find_client(client_path: str) -> Path:
    """The CS2 client at `client_path`, or the first one in any Steam library."""
    if client_path:
        root = Path(client_path).expanduser()
        if not is_client(root):
            raise VoltmodError(f"no CS2 client at {root}\nExpected {root / GAMEINFO}")
        return root

    for candidate in _STEAM_ROOTS:
        steam = Path(candidate).expanduser()
        if steam.is_dir():
            for library in _steam_libraries(steam):
                if is_client(library / _CS2_IN_STEAM_LIBRARY):
                    return library / _CS2_IN_STEAM_LIBRARY

    raise VoltmodError("no CS2 client found; set CS2_CLIENT_PATH in .env or pass --client-path")


def _steam_libraries(steam: Path) -> list[Path]:
    """Every Steam library on this machine, so a client on a second drive is still found."""
    libraries = [steam]
    manifest = steam / "steamapps/libraryfolders.vdf"
    if manifest.is_file():
        text = manifest.read_text(encoding="utf-8", errors="replace")
        libraries += [Path(path.replace("\\\\", "/")) for path in _LIBRARY_PATH.findall(text)]
    return libraries
