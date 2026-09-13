"""The init, new-plugin and doctor commands."""

import sys
from typing import Annotated

import typer

from voltmod.check_results import print_results
from voltmod.doctor import run_checks
from voltmod.project import Project
from voltmod.scaffold import create_plugin, create_project, is_kebab_case

setup_commands = typer.Typer()


def _kebab_case(value: str | None) -> str | None:
    if value is not None and not is_kebab_case(value):
        raise typer.BadParameter(f"'{value}' is not kebab-case (expected e.g. 'fun-votes')")
    return value


@setup_commands.command("init")
def init_command(
    name: Annotated[
        str | None,
        typer.Option(
            "--name",
            callback=_kebab_case,
            help="Kebab-case project name (default: the directory name)",
        ),
    ] = None,
    plugin: Annotated[
        str,
        typer.Option("--plugin", callback=_kebab_case, help="Kebab-case name for the first plugin"),
    ] = "my-plugin",
) -> None:
    """Stamp a whole consumer project into the working directory."""
    project = Project.load()
    project_name = _kebab_case(name or project.root.name) or project.root.name
    create_project(project.root, project_name, plugin)


@setup_commands.command("new-plugin")
def new_plugin_command(
    name: Annotated[
        str, typer.Argument(callback=_kebab_case, help="Kebab-case name, e.g. fun-votes")
    ],
) -> None:
    """Stamp a plugin skeleton into plugins/<name>/."""
    create_plugin(Project.load().root, name)


@setup_commands.command("doctor")
def doctor_command(
    server_path: Annotated[
        str, typer.Option("--server-path", help="Optional CS2 server root to check")
    ] = "",
) -> None:
    """Check the local toolchain, the project, and an optional server."""
    project = Project.load()
    print(f"VoltMod doctor\nProject: {project.root.resolve()}\nPython: {sys.version.split()[0]}")
    if print_results(run_checks(project, server_path)):
        raise typer.Exit(1)
