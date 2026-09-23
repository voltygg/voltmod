from typing import Annotated

import typer

from voltmod.options import current_project
from voltmod.scaffold.scaffold import create_plugin, create_project, is_kebab_case


def _kebab_case(value: str | None) -> str | None:
    if value is not None and not is_kebab_case(value):
        raise typer.BadParameter(f"'{value}' is not kebab-case (expected e.g. 'fun-votes')")
    return value


def project_command(
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
    root = current_project().root
    project_name = _kebab_case(name or root.name) or root.name
    create_project(root, project_name, plugin)


def plugin_command(
    name: Annotated[
        str, typer.Argument(callback=_kebab_case, help="Kebab-case name, e.g. fun-votes")
    ],
) -> None:
    """Stamp a plugin skeleton into plugins/<name>/."""
    create_plugin(current_project().root, name)
