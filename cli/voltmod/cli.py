import subprocess
import sys
from collections.abc import Callable

import typer

from voltmod.checks import cli as checks
from voltmod.database import cli as database
from voltmod.errors import VoltmodError
from voltmod.framework import cli as framework
from voltmod.panorama import cli as panorama
from voltmod.scaffold import cli as scaffold
from voltmod.server import cli as server
from voltmod.toolchain import cli as toolchain
from voltmod.toolchain.process import put_tools_first_on_path

app = typer.Typer(
    help="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    no_args_is_help=True,
)
app.command("init")(scaffold.init_command)
app.command("new-plugin")(scaffold.new_plugin_command)
app.command("bootstrap")(toolchain.bootstrap_command)
app.command("build")(toolchain.build_command)
app.command("test")(toolchain.test_command)
app.command("install")(server.install_command)
app.command("serve")(server.serve_command)
app.command("doctor")(checks.doctor_command)
app.command("lint")(checks.lint_command)
app.command("format")(checks.format_command)
app.command("modgraph")(framework.modgraph_command)
app.command("schemagen")(framework.schemagen_command)

panorama_app = typer.Typer(help="Render, check and compile Panorama screens.", no_args_is_help=True)
panorama_app.command("render")(panorama.render_command)
panorama_app.command("check")(panorama.check_command)
panorama_app.command("compile")(panorama.compile_command)
app.add_typer(panorama_app, name="panorama")

database_app = typer.Typer(
    help="Migration rendering and table-header generation.", no_args_is_help=True
)
database_app.command("sql")(database.sql_command)
database_app.command("tables")(database.tables_command)
app.add_typer(database_app, name="database")

gamedata_app = typer.Typer(
    help="Check and repair gamedata against the shipped game binaries.", no_args_is_help=True
)
gamedata_app.command("check")(framework.gamedata_check_command)
gamedata_app.command("fetch")(framework.gamedata_fetch_command)
gamedata_app.command("resolve")(framework.gamedata_resolve_command)
app.add_typer(gamedata_app, name="gamedata")


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
