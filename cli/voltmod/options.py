from pathlib import Path
from typing import Annotated

import typer

from voltmod.project import Project

_project: Project | None = None

Preset = Annotated[
    str, typer.Option("--preset", "-p", envvar="CS2_BUILD_PRESET", help="CMake preset")
]
ServerDir = Annotated[
    Path | None,
    typer.Option("--server", envvar="CS2_SERVER_PATH", file_okay=False, help="CS2 server root"),
]
PluginNames = Annotated[
    list[str] | None,
    typer.Argument(metavar="[PLUGIN]...", help="Default: every plugin", show_default=False),
]


def use_project(project: Project) -> None:
    """Called once by the root callback, before any command runs."""
    global _project
    _project = project


def current_project() -> Project:
    return _project or Project.load()
