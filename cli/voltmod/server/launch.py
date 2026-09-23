from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.platforms import Platform
from voltmod.project import Settings
from voltmod.server.cs2_server import Cs2Server
from voltmod.steam import CS2_APP
from voltmod.toolchain.process import run


def update_server(steamcmd_path: str, server: Path) -> None:
    """Refresh the server files when SteamCMD is available."""
    steamcmd = Path(steamcmd_path).expanduser() if steamcmd_path else None
    if not steamcmd or not steamcmd.is_file():
        print(f"WARNING: SteamCMD not found at {steamcmd}; skipping update.")
        return

    login: list[str | Path] = ["+force_install_dir", server, "+login", "anonymous"]
    result = run(steamcmd, *login, "+app_update", CS2_APP, "validate", "+quit", check=False)
    if result.returncode:
        print(f"WARNING: SteamCMD update failed ({result.returncode}); using existing files.")


def run_server(settings: Settings, *, check_update: bool = False) -> None:
    """Optionally update, then run the dedicated server in the foreground."""
    server = Cs2Server.open(settings.server_path)
    if check_update:
        update_server(settings.steamcmd_path, server.root)

    if server.restore_metamod_search_path():
        print("Restored Metamod's search path in gameinfo.gi (a CS2 update removed it).")

    executable = server.executable
    if executable is None:
        expected = server.root / Platform.host().server_executable
        raise VoltmodError(f"CS2 executable not found: {expected}")

    command: list[str | Path] = [executable, "-dedicated", "-console", "-usercon"]
    command += ["+map", settings.map_name]
    command += ["-maxplayers", str(settings.max_players), "-port", str(settings.port)]
    command += ["+game_mode", "0"]
    if settings.gslt_token:
        command += ["+sv_setsteamaccount", settings.gslt_token]
    if settings.rcon_password:
        command += ["+rcon_password", settings.rcon_password]

    mode = "public" if settings.gslt_token else "LAN"
    print(
        f"=== Starting CS2: {settings.map_name}, {settings.max_players} players, "
        f"port {settings.port}, {mode} ==="
    )
    run(*command, cwd=executable.parent, check=False)
