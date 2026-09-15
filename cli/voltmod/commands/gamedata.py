"""The `voltmod gamedata` commands: check patterns, and repair explained drift."""

from typing import Annotated

import typer

from voltmod.gamedata import (
    GAMEDATA_FILE,
    PatternResult,
    PatternStatus,
    check_gamedata,
    write_repairs,
)
from voltmod.project import Project

gamedata_commands = typer.Typer(help="Check and repair gamedata against the shipped game binaries.")

GameDir = Annotated[
    str,
    typer.Option(
        "--game-dir", help="CS2 install, or a folder of its binaries (default: CS2_SERVER_PATH)"
    ),
]
Platform = Annotated[
    str, typer.Option("--platform", help="windows or linux (default: what --game-dir holds)")
]


@gamedata_commands.command("check")
def check_command(game_dir: GameDir = "", platform: Platform = "") -> None:
    """Report which committed patterns no longer match the shipped binaries."""
    project = Project.load()
    _, results = check_gamedata(project.root, game_dir or project.settings.server_path, platform)
    drifted = _print_drift(results)
    if drifted:
        print(f"{drifted} entries drifted; repair them with: voltmod gamedata resolve --write")
        raise typer.Exit(1)


@gamedata_commands.command("resolve")
def resolve_command(
    game_dir: GameDir = "",
    platform: Platform = "",
    write: Annotated[bool, typer.Option("--write", help="Patch gamedata.jsonc in place")] = False,
) -> None:
    """Repair the patterns that drifted, leaving every entry that still matches alone."""
    project = Project.load()
    text, results = check_gamedata(project.root, game_dir or project.settings.server_path, platform)
    drifted = _print_drift(results)

    repaired = [result for result in results if result.status is PatternStatus.REPAIRED]
    if not write:
        if repaired:
            print(f"{len(repaired)} entries would be rewritten (pass --write)")
        raise typer.Exit(1 if drifted else 0)

    if repaired:
        write_repairs(project.root, text, repaired)
        print(f"wrote {GAMEDATA_FILE} ({len(repaired)} patterns)")

    print("A unique match is not proof of behaviour: exercise each feature on a live server.")
    if drifted - len(repaired):
        raise typer.Exit(1)


def _print_drift(results: list[PatternResult]) -> int:
    """Print every pattern that no longer holds; return how many there are."""
    held = sum(result.status is PatternStatus.HOLDS for result in results)
    print(f"    {held}/{len(results)} patterns hold")
    for result in results:
        if result.status is PatternStatus.HOLDS:
            continue
        print(f"    {result.status.upper():9} {result.section}.{result.key}")
        for line in result.detail.splitlines():
            print(f"{'':14}{line}")
    return len(results) - held
