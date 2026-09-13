"""The `voltmod` command line: build, scaffold and check CS2 Metamod:Source plugin projects."""

import typer

from voltmod.commands.build import build_commands
from voltmod.commands.database import database_commands
from voltmod.commands.gamedata import gamedata_commands
from voltmod.commands.lint import lint_commands
from voltmod.commands.panorama import panorama_commands
from voltmod.commands.schemagen import schemagen_commands
from voltmod.commands.server import server_commands
from voltmod.commands.setup import setup_commands
from voltmod.process import run_with_error_messages

app = typer.Typer(
    help="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    no_args_is_help=True,
)
app.add_typer(build_commands)
app.add_typer(server_commands)
app.add_typer(setup_commands)
app.add_typer(lint_commands)
app.add_typer(schemagen_commands)
app.add_typer(database_commands, name="database")
app.add_typer(gamedata_commands, name="gamedata")
app.add_typer(panorama_commands, name="panorama")


def main() -> None:
    run_with_error_messages(app)
