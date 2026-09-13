"""The `voltmod` command line: build, scaffold and check CS2 Metamod:Source plugin projects."""

import subprocess
import sys

import typer

from voltmod.commands.build import build_commands
from voltmod.commands.database import database_commands
from voltmod.commands.gamedata import gamedata_commands
from voltmod.commands.lint import lint_commands
from voltmod.commands.panorama import panorama_commands
from voltmod.commands.schemagen import schemagen_commands
from voltmod.commands.server import server_commands
from voltmod.commands.setup import setup_commands
from voltmod.errors import VoltmodError
from voltmod.process import put_tools_first_on_path

app = typer.Typer(
    help="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    no_args_is_help=True,
)
app.add_typer(build_commands)
app.add_typer(server_commands)
app.add_typer(setup_commands)
app.add_typer(lint_commands)
app.add_typer(database_commands, name="database")
app.add_typer(gamedata_commands, name="gamedata")
app.add_typer(panorama_commands, name="panorama")
app.add_typer(schemagen_commands, name="schemagen")


def run_with_error_messages(typer_app: typer.Typer) -> None:
    """Run @p typer_app, reporting a VoltmodError or a failed tool as one line and exit code 1."""
    put_tools_first_on_path()
    try:
        typer_app()
    except VoltmodError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as error:
        command = error.cmd if isinstance(error.cmd, str) else " ".join(map(str, error.cmd))
        print(f"error: `{command}` exited with {error.returncode}", file=sys.stderr)
        sys.exit(1)


def main() -> None:
    run_with_error_messages(app)
