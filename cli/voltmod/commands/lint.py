"""The modgraph command: module layering and source conventions."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod.check_results import print_results
from voltmod.project import Project
from voltmod.source_rules import check_framework, check_plugins

lint_commands = typer.Typer()


@lint_commands.command("modgraph")
def modgraph_command(
    plugins: Annotated[
        Path | None,
        typer.Option(
            "--plugins",
            help="Check a consumer repo's plugins/ for the source conventions instead of "
            "the framework's module layering",
        ),
    ] = None,
) -> None:
    """Check VoltMod's module layering, or a consumer's plugin sources."""
    if plugins is not None:
        if print_results(check_plugins(plugins)):
            raise typer.Exit(1)
        print("Plugin sources hold.")
        return

    dependencies, results = check_framework(Project.load().root)
    for module, used in dependencies.items():
        print(f"{module:10} -> {' '.join(sorted(used)) or '(none)'}")
    if print_results(results):
        raise typer.Exit(1)
    print("\nLayering holds.")
