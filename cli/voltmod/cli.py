"""The `voltmod` command line: build, scaffold and check CS2 Metamod:Source plugin projects."""

import subprocess
import sys
from collections.abc import Callable

import typer

from voltmod.commands.build import build_commands
from voltmod.commands.checks import check_commands
from voltmod.commands.database import database_commands
from voltmod.commands.framework import framework_commands, gamedata_commands
from voltmod.commands.local_server import local_server_commands
from voltmod.commands.panorama import panorama_commands
from voltmod.commands.scaffold import scaffold_commands
from voltmod.errors import VoltmodError
from voltmod.toolchain.process import put_tools_first_on_path

app = typer.Typer(
    help="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    no_args_is_help=True,
)
app.add_typer(build_commands)
app.add_typer(local_server_commands)
app.add_typer(scaffold_commands)
app.add_typer(check_commands)
app.add_typer(framework_commands)
app.add_typer(database_commands, name="database")
app.add_typer(gamedata_commands, name="gamedata")
app.add_typer(panorama_commands, name="panorama")


def run_cli(main: Callable[[], object]) -> None:
    """Run `main`, reporting a VoltmodError or a failed tool as one line and exit code 1."""
    put_tools_first_on_path()
    try:
        main()
    except VoltmodError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as error:
        command = error.cmd if isinstance(error.cmd, str) else " ".join(map(str, error.cmd))
        print(f"error: `{command}` exited with {error.returncode}", file=sys.stderr)
        sys.exit(1)


def main() -> None:
    run_cli(app)
