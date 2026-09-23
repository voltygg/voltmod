import subprocess
from pathlib import Path

from voltmod.cs2_install import (
    CS2_APP,
    SERVER_EXECUTABLES,
    find_server,
    restore_metamod_search_path,
    server_executable,
)
from voltmod.errors import VoltmodError
from voltmod.project import Settings
from voltmod.toolchain.process import WINDOWS


def update_server(steamcmd_path: str, server: Path) -> None:
    """Refresh the server files when SteamCMD is available."""
    steamcmd = Path(steamcmd_path).expanduser() if steamcmd_path else None
    if not steamcmd or not steamcmd.is_file():
        print(f"WARNING: SteamCMD not found at {steamcmd}; skipping update.")
        return

    # fmt: off
    update = [
        str(steamcmd), "+force_install_dir", str(server), "+login", "anonymous",
        "+app_update", CS2_APP, "validate", "+quit",
    ]
    # fmt: on
    result = subprocess.run(update)
    if result.returncode:
        print(f"WARNING: SteamCMD update failed ({result.returncode}); using existing files.")


def run_server(settings: Settings, *, check_update: bool = False) -> None:
    """Optionally update, then run the dedicated server in the foreground."""
    server = find_server(settings.server_path)
    if check_update:
        update_server(settings.steamcmd_path, server)

    if restore_metamod_search_path(server):
        print("Restored Metamod's search path in gameinfo.gi (a CS2 update removed it).")

    executable = server_executable(server)
    if executable is None:
        expected = SERVER_EXECUTABLES[0] if WINDOWS else SERVER_EXECUTABLES[1]
        raise VoltmodError(f"CS2 executable not found: {server / expected}")

    # fmt: off
    command = [
        str(executable), "-dedicated", "-console", "-usercon",
        "+map", settings.map_name,
        "-maxplayers", str(settings.max_players),
        "-port", str(settings.port),
        "+game_mode", "0",
    ]
    # fmt: on
    if settings.gslt_token:
        command += ["+sv_setsteamaccount", settings.gslt_token]
    if settings.rcon_password:
        command += ["+rcon_password", settings.rcon_password]

    mode = "public" if settings.gslt_token else "LAN"
    print(
        f"=== Starting CS2: {settings.map_name}, {settings.max_players} players, "
        f"port {settings.port}, {mode} ==="
    )
    subprocess.run(command, cwd=executable.parent)
