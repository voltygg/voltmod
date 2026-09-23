from pathlib import Path
from typing import Annotated

import typer

from voltmod.options import PluginNames, Preset, ServerDir, current_project
from voltmod.project import default_preset
from voltmod.server.cs2_server import Cs2Server
from voltmod.server.install import install_plugins
from voltmod.server.launch import LaunchOptions, run_server
from voltmod.toolchain.build import build

MapName = Annotated[str, typer.Option("--map", envvar="CS2_MAP")]
Port = Annotated[int, typer.Option("--port", envvar="CS2_PORT")]
MaxPlayers = Annotated[int, typer.Option("--max-players", envvar="CS2_MAX_PLAYERS")]
GsltToken = Annotated[
    str, typer.Option("--gslt-token", envvar="GSLT_TOKEN", help="Empty starts in LAN mode")
]
RconPassword = Annotated[str, typer.Option("--rcon-password", envvar="RCON_PASSWORD")]


def install_command(
    plugins: PluginNames = None, preset: Preset = default_preset(), server: ServerDir = None
) -> None:
    """Install built plugins, and the host they load under, into a local CS2 server."""
    install_plugins(current_project(), Cs2Server.open(server), plugins or [], preset)


def serve_command(
    server: ServerDir = None,
    map_name: MapName = "de_dust2",
    port: Port = 27015,
    max_players: MaxPlayers = 16,
    gslt_token: GsltToken = "",
    rcon_password: RconPassword = "",
    update: Annotated[
        bool, typer.Option("--update", help="Refresh the server with SteamCMD first")
    ] = False,
    steamcmd: Annotated[
        Path | None, typer.Option("--steamcmd", envvar="STEAMCMD_PATH", dir_okay=False)
    ] = None,
) -> None:
    """Run the local CS2 dedicated server in the foreground."""
    options = LaunchOptions(map_name, port, max_players, gslt_token, rcon_password)
    run_server(Cs2Server.open(server), options, update=update, steamcmd=steamcmd)


def run_command(
    plugins: PluginNames = None,
    preset: Preset = default_preset(),
    server: ServerDir = None,
    map_name: MapName = "de_dust2",
    port: Port = 27015,
    max_players: MaxPlayers = 16,
    gslt_token: GsltToken = "",
    rcon_password: RconPassword = "",
) -> None:
    """Build, install the plugins into the local server, and start it."""
    project = current_project()
    # Fail on a bad plugin name or server path before spending a whole build on it.
    project.installable_plugins(plugins or [])
    game = Cs2Server.open(server)

    build(project, preset)
    install_plugins(project, game, plugins or [], preset)
    run_server(game, LaunchOptions(map_name, port, max_players, gslt_token, rcon_password))
