"""The `voltmod schemagen` command: regenerate the schema accessor layer from a dump."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod.cs2_install import SCHEMA_DUMP, find_server
from voltmod.files import read_json
from voltmod.project import Project
from voltmod.schemagen.generate import MANIFEST, render_outputs, write_outputs

schemagen_commands = typer.Typer()


@schemagen_commands.command("schemagen")
def schemagen_command(
    dump_path: Annotated[
        str, typer.Option("--dump", help="Schema dump (default: the one the local server wrote)")
    ] = "",
    server_path: Annotated[
        str, typer.Option("--server-path", help="CS2 server root (default: CS2_SERVER_PATH)")
    ] = "",
) -> None:
    """Regenerate the schema accessor layer from a dump."""
    project = Project.load()
    manifest = read_json(project.root / MANIFEST, "manifest")
    if dump_path:
        dump_file = Path(dump_path)
    else:
        dump_file = find_server(server_path or project.settings.server_path) / SCHEMA_DUMP

    output = render_outputs(read_json(dump_file, "dump"), manifest)
    write_outputs(project.root, output.files)
    print(output.summary)
