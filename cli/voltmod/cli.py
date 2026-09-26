import os
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Annotated

import typer

from voltmod import console
from voltmod.checks import cli as checks
from voltmod.database import cli as database
from voltmod.errors import VoltmodError
from voltmod.framework import cli as framework
from voltmod.panorama import cli as panorama
from voltmod.project import Project
from voltmod.scaffold import cli as scaffold
from voltmod.server import cli as server
from voltmod.toolchain import cli as toolchain
from voltmod.toolchain.process import put_tools_first_on_path


def _group(help: str) -> typer.Typer:
    return typer.Typer(help=help, no_args_is_help=True)


app = _group("Build, run and check CS2 plugin projects.")


@app.callback()
def _load_project(
    directory: Annotated[
        Path,
        typer.Option(
            "-C",
            "--directory",
            exists=True,
            file_okay=False,
            help="Run as if started in this directory",
        ),
    ] = Path(),
) -> None:
    os.chdir(directory)
    # Before any subcommand parses its options, so the .env defaults reach their envvars.
    Project.load()


new = _group("Stamp a new project or plugin from the bundled templates.")
new.command("project")(scaffold.project_command)
new.command("plugin")(scaffold.plugin_command)
app.add_typer(new, name="new")

app.command("bootstrap")(toolchain.bootstrap_command)
app.command("build")(toolchain.build_command)
app.command("test")(toolchain.test_command)
app.command("install")(server.install_command)
app.command("serve")(server.serve_command)
app.command("run")(server.run_command)
app.command("doctor")(checks.doctor_command)
app.command("lint")(checks.lint_command)
app.command("format")(checks.format_command)

panorama_group = _group("Render, check and compile Panorama screens.")
panorama_group.command("render")(panorama.render_command)
panorama_group.command("check")(panorama.check_command)
panorama_group.command("compile")(panorama.compile_command)
app.add_typer(panorama_group, name="panorama")

database_group = _group("Render migrations and generate their table headers.")
database_group.command("sql")(database.sql_command)
database_group.command("header")(database.header_command)
app.add_typer(database_group, name="database")

gamedata = _group("Check and repair gamedata against the shipped game binaries.")
gamedata.command("check")(framework.gamedata_check_command)
gamedata.command("fetch")(framework.gamedata_fetch_command)

framework_group = _group("Maintain the VoltMod checkout itself.")
framework_group.command("schemagen")(framework.schemagen_command)
framework_group.add_typer(gamedata, name="gamedata")
app.add_typer(framework_group, name="framework")


def run_cli(main: Callable[[], object]) -> None:
    """Run `main`, reporting a VoltmodError or a failed tool as one line and exit code 1."""
    put_tools_first_on_path()
    try:
        main()
    except VoltmodError as error:
        console.error(str(error))
        sys.exit(1)
    except subprocess.CalledProcessError as error:
        command = error.cmd if isinstance(error.cmd, str) else " ".join(map(str, error.cmd))
        console.error(f"`{command}` exited with {error.returncode}")
        sys.exit(1)


def main() -> None:
    run_cli(app)
