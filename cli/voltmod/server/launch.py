from dataclasses import dataclass
from pathlib import Path

from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.platforms import Platform
from voltmod.server.cs2_server import Cs2Server
from voltmod.steam import CS2_APP
from voltmod.toolchain.process import run


@dataclass(frozen=True, slots=True)
class LaunchOptions:
    map_name: str
    port: int
    max_players: int
    gslt_token: str
    rcon_password: str


def update_game(steamcmd: Path | None, server: Path) -> None:
    """Refresh the server files when SteamCMD is available."""
    steamcmd = steamcmd.expanduser() if steamcmd else None
    if not steamcmd or not steamcmd.is_file():
        console.warn(f"SteamCMD not found at {steamcmd}; skipping the update")
        return

    login: list[str | Path] = ["+force_install_dir", server, "+login", "anonymous"]
    result = run(steamcmd, *login, "+app_update", CS2_APP, "validate", "+quit", check=False)
    if result.returncode:
        console.warn(f"SteamCMD update failed ({result.returncode}); using the existing files")


def run_server(
    server: Cs2Server, options: LaunchOptions, *, update: bool = False, steamcmd: Path | None = None
) -> None:
    """Optionally update with SteamCMD, then run the dedicated server in the foreground."""
    if update:
        update_game(steamcmd, server.root)

    if server.restore_voltmod_search_path():
        console.note("Restored VoltMod's search path in gameinfo.gi (a CS2 update removed it)")

    executable = server.executable
    if executable is None:
        expected = server.root / Platform.host().server_executable
        raise VoltmodError(f"CS2 executable not found: {expected}")

    command: list[str | Path] = [executable, "-dedicated", "-console", "-usercon"]
    command += ["+map", options.map_name]
    command += ["-maxplayers", str(options.max_players), "-port", str(options.port)]
    command += ["+game_mode", "0"]
    if options.gslt_token:
        command += ["+sv_setsteamaccount", options.gslt_token]
    if options.rcon_password:
        command += ["+rcon_password", options.rcon_password]

    mode = "public" if options.gslt_token else "LAN"
    console.step(
        f"Starting CS2: {options.map_name}, {options.max_players} players, "
        f"port {options.port}, {mode}"
    )
    run(*command, cwd=executable.parent, check=False)
