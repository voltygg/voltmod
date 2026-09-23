"""Commands that maintain the framework checkout: modgraph, schemagen, and gamedata."""

import sys
from pathlib import Path
from typing import Annotated

import typer

from voltmod.commands.shared import ServerPath, exit_on_failure
from voltmod.cs2_install import GAME_LIBRARIES, SCHEMA_DUMP, find_server
from voltmod.errors import VoltmodError
from voltmod.files import read_json
from voltmod.framework.game_builds import (
    archive_resolved,
    archived_builds,
    default_archive,
    fetch_build,
)
from voltmod.framework.gamedata import PatternResult, PatternStatus, check_gamedata, write_repairs
from voltmod.framework.modgraph import check_framework
from voltmod.framework.paths import GAMEDATA_FILE, SCHEMA_BASELINES, SCHEMA_MANIFEST
from voltmod.framework.schemagen.generate import render_outputs, write_outputs
from voltmod.project import Project

framework_commands = typer.Typer()
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


@framework_commands.command("modgraph")
def modgraph_command() -> None:
    """Check VoltMod's module layering and the framework's own source rules."""
    dependencies, results = check_framework(Project.load().root)
    for module, used in dependencies.items():
        print(f"{module:10} -> {' '.join(sorted(used)) or '(none)'}")
    exit_on_failure(results)
    print("\nLayering holds.")


@framework_commands.command("schemagen")
def schemagen_command(
    dump_path: Annotated[
        str, typer.Option("--dump", help="Schema dump (default: the one the local server wrote)")
    ] = "",
    server_path: ServerPath = "",
    platform: Annotated[
        str, typer.Option("--platform", help="windows or linux: the server the dump came from")
    ] = "windows" if sys.platform == "win32" else "linux",
) -> None:
    """Regenerate the schema accessor layer from a dump."""
    if platform not in SCHEMA_BASELINES:
        raise VoltmodError(f"unknown platform '{platform}'; use windows or linux")
    project = Project.load()
    manifest = read_json(project.root / SCHEMA_MANIFEST, "manifest")
    if dump_path:
        dump_file = Path(dump_path)
    else:
        dump_file = find_server(project.server_path(server_path)) / SCHEMA_DUMP

    output = render_outputs(read_json(dump_file, "dump"), manifest, platform)
    write_outputs(project.root, output.files, platform)
    print(output.summary)


@gamedata_commands.command("check")
def check_command(game_dir: GameDir = "", platform: Platform = "") -> None:
    """Report which committed patterns no longer match the shipped binaries."""
    project = Project.load()
    _, results = check_gamedata(project.root, project.server_path(game_dir), platform)
    drifted = _print_drift(results)
    if drifted:
        print(f"{drifted} entries drifted; repair them with: voltmod gamedata resolve --write")
        raise typer.Exit(1)


@gamedata_commands.command("fetch")
def fetch_command(
    platform: Annotated[
        str, typer.Option("--platform", help="windows or linux (default: both)")
    ] = "",
    server_path: ServerPath = "",
) -> None:
    """Archive the current build's server binaries from Steam, for checks and old/new diffs.

    The archive is CS2_BUILD_ARCHIVE, else ~/.voltmod/cs2-builds.
    """
    root = default_archive()
    server = Project.load().server_path(server_path)
    for name in [platform] if platform else list(GAME_LIBRARIES):
        if name not in GAME_LIBRARIES:
            raise VoltmodError(f"unknown platform '{name}'; use windows or linux")
        print(f"==> fetch {name}")
        target = fetch_build(root, name)
        print(f"    {target}")
        if server and (build := archive_resolved(Path(server).expanduser(), root, name)):
            print(f"    kept the local server's resolved.{name}.json under build {build}")
        print(f"    voltmod gamedata check --game-dir {target} --platform {name}")
    builds = archived_builds(root)
    if len(builds) > 1:
        print(f"Archived builds: {' '.join(builds)} (the previous one is {builds[-2]})")


@gamedata_commands.command("resolve")
def resolve_command(
    game_dir: GameDir = "",
    platform: Platform = "",
    write: Annotated[bool, typer.Option("--write", help="Patch gamedata.jsonc in place")] = False,
) -> None:
    """Repair the patterns that drifted, leaving every entry that still matches alone."""
    project = Project.load()
    text, results = check_gamedata(project.root, project.server_path(game_dir), platform)
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
