"""Commands that maintain the framework checkout: modgraph, schemagen, and gamedata."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod.checks.results import exit_on_failure
from voltmod.files import read_json
from voltmod.framework.game_builds import (
    archive_resolved,
    archived_builds,
    default_archive,
    fetch_build,
)
from voltmod.framework.gamedata import PatternResult, PatternStatus, check_gamedata, write_repairs
from voltmod.framework.layering import check_framework
from voltmod.framework.paths import GAMEDATA_FILE, SCHEMA_MANIFEST
from voltmod.framework.schemagen.generate import render_outputs, write_outputs
from voltmod.options import ServerPath
from voltmod.platforms import Platform
from voltmod.project import Project
from voltmod.server.cs2_server import Cs2Server
from voltmod.server.install import SCHEMA_DUMP

GameDir = Annotated[
    str,
    typer.Option(
        "--game-dir", help="CS2 install, or a folder of its binaries (default: CS2_SERVER_PATH)"
    ),
]
PlatformOption = Annotated[
    Platform | None, typer.Option("--platform", help="Default: what --game-dir holds")
]


def modgraph_command() -> None:
    """Check VoltMod's module layering and the framework's own source rules."""
    dependencies, results = check_framework(Project.load().root)
    for module, used in dependencies.items():
        print(f"{module:10} -> {' '.join(sorted(used)) or '(none)'}")
    exit_on_failure(results)
    print("\nLayering holds.")


def schemagen_command(
    dump_path: Annotated[
        str, typer.Option("--dump", help="Schema dump (default: the one the local server wrote)")
    ] = "",
    server_path: ServerPath = "",
    platform: Annotated[
        Platform, typer.Option("--platform", help="The server the dump came from")
    ] = Platform.host(),
) -> None:
    """Regenerate the schema accessor layer from a dump."""
    project = Project.load()
    manifest = read_json(project.root / SCHEMA_MANIFEST, "manifest")
    if dump_path:
        dump_file = Path(dump_path)
    else:
        dump_file = Cs2Server.open(project.server_path(server_path)).root / SCHEMA_DUMP

    output = render_outputs(read_json(dump_file, "dump"), manifest, platform)
    write_outputs(project.root, output.files, platform)
    print(output.summary)


def gamedata_check_command(game_dir: GameDir = "", platform: PlatformOption = None) -> None:
    """Report which committed patterns no longer match the shipped binaries."""
    project = Project.load()
    _, results = check_gamedata(project.root, project.server_path(game_dir), platform)
    drifted = _print_drift(results)
    if drifted:
        print(f"{drifted} entries drifted; repair them with: voltmod gamedata resolve --write")
        raise typer.Exit(1)


def gamedata_fetch_command(
    platform: Annotated[Platform | None, typer.Option("--platform", help="Default: both")] = None,
    server_path: ServerPath = "",
) -> None:
    """Archive the current build's server binaries from Steam, for checks and old/new diffs.

    The archive is CS2_BUILD_ARCHIVE, else ~/.voltmod/cs2-builds.
    """
    root = default_archive()
    server = Project.load().server_path(server_path)
    for name in [platform] if platform else list(Platform):
        print(f"==> fetch {name}")
        target = fetch_build(root, name)
        print(f"    {target}")
        if server and (build := archive_resolved(Path(server).expanduser(), root, name)):
            print(f"    kept the local server's resolved.{name}.json under build {build}")
        print(f"    voltmod gamedata check --game-dir {target} --platform {name}")
    builds = archived_builds(root)
    if len(builds) > 1:
        print(f"Archived builds: {' '.join(builds)} (the previous one is {builds[-2]})")


def gamedata_resolve_command(
    game_dir: GameDir = "",
    platform: PlatformOption = None,
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
