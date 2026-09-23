"""The install and serve commands for a local CS2 server."""

from typing import Annotated

import typer

from voltmod.options import ServerPath
from voltmod.project import Project
from voltmod.server.install import install_plugins
from voltmod.server.launch import run_server


def install_command(
    plugin: Annotated[
        str, typer.Argument(help="Plugin to install (default: every built plugin)")
    ] = "",
    preset: Annotated[
        str | None, typer.Option("--preset", help="Build directory to install from")
    ] = None,
    server_path: ServerPath = "",
) -> None:
    """Install already-built plugins into a local CS2 server."""
    project = Project.load()
    preset = project.resolve_preset(preset)
    install_plugins(project, project.server_path(server_path), plugin, preset)


def serve_command(
    server_path: ServerPath = "",
    steamcmd_path: Annotated[
        str, typer.Option("--steamcmd-path", help="SteamCMD executable (default: STEAMCMD_PATH)")
    ] = "",
    map_name: Annotated[str, typer.Option("--map", help="Default: CS2_MAP, else de_dust2")] = "",
    port: Annotated[
        int | None, typer.Option("--port", help="Default: CS2_PORT, else 27015")
    ] = None,
    max_players: Annotated[
        int | None, typer.Option("--max-players", help="Default: CS2_MAX_PLAYERS, else 16")
    ] = None,
    gslt_token: Annotated[str, typer.Option("--gslt-token", help="Default: GSLT_TOKEN")] = "",
    rcon_password: Annotated[
        str, typer.Option("--rcon-password", help="Default: RCON_PASSWORD")
    ] = "",
    check_update: Annotated[
        bool, typer.Option("--check-update", help="Refresh the server with SteamCMD first")
    ] = False,
) -> None:
    """Run the local CS2 dedicated server in the foreground."""
    settings = Project.load().settings.with_options(
        server_path=server_path,
        steamcmd_path=steamcmd_path,
        map_name=map_name,
        port=port,
        max_players=max_players,
        gslt_token=gslt_token,
        rcon_password=rcon_password,
    )
    run_server(settings, check_update=check_update)
