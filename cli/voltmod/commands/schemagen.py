"""The `voltmod schemagen` command: regenerate the schema accessor layer from a dump."""

import sys
from pathlib import Path
from typing import Annotated

import typer

from voltmod.cs2_install import SCHEMA_DUMP, find_server
from voltmod.errors import VoltmodError
from voltmod.files import read_json
from voltmod.project import Project
from voltmod.schemagen.generate import BASELINES, MANIFEST, render_outputs, write_outputs

schemagen_commands = typer.Typer()


@schemagen_commands.command("schemagen")
def schemagen_command(
    dump_path: Annotated[
        str, typer.Option("--dump", help="Schema dump (default: the one the local server wrote)")
    ] = "",
    server_path: Annotated[
        str, typer.Option("--server-path", help="CS2 server root (default: CS2_SERVER_PATH)")
    ] = "",
    platform: Annotated[
        str, typer.Option("--platform", help="windows or linux: the server the dump came from")
    ] = "windows" if sys.platform == "win32" else "linux",
) -> None:
    """Regenerate the schema accessor layer from a dump."""
    if platform not in BASELINES:
        raise VoltmodError(f"unknown platform '{platform}'; use windows or linux")
    project = Project.load()
    manifest = read_json(project.root / MANIFEST, "manifest")
    if dump_path:
        dump_file = Path(dump_path)
    else:
        dump_file = find_server(server_path or project.settings.server_path) / SCHEMA_DUMP

    output = render_outputs(read_json(dump_file, "dump"), manifest, platform)
    write_outputs(project.root, output.files, platform)
    print(output.summary)
