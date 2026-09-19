"""Options and result handling more than one command module uses."""

from collections.abc import Iterable
from typing import Annotated

import typer

from voltmod.check_results import CheckResult, print_results

ServerPath = Annotated[
    str, typer.Option("--server-path", help="CS2 server root (default: CS2_SERVER_PATH)")
]
PresetArgument = Annotated[
    str | None,
    typer.Argument(help="CMake preset (default: CS2_BUILD_PRESET, else release for this OS)"),
]


def exit_on_failure(results: Iterable[CheckResult]) -> None:
    """Print `results`, and exit 1 when any of them failed."""
    if print_results(results):
        raise typer.Exit(1)
