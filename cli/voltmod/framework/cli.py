from pathlib import Path
from typing import Annotated

import typer

from voltmod import console
from voltmod.files import read_json
from voltmod.framework.game_builds import (
    archive_dir,
    archive_resolved,
    archived_builds,
    download_build,
)
from voltmod.framework.gamedata import PatternResult, PatternStatus, check_gamedata, write_repairs
from voltmod.framework.paths import GAMEDATA_FILE, SCHEMA_MANIFEST
from voltmod.framework.schemagen.generate import render_schema, write_schema
from voltmod.options import ServerDir, current_project
from voltmod.platforms import Platform
from voltmod.server.cs2_server import Cs2Server
from voltmod.server.install import SCHEMA_DUMP

GameDir = Annotated[
    Path | None,
    typer.Option(
        "--game-dir",
        envvar="CS2_SERVER_PATH",
        file_okay=False,
        help="CS2 install, or a folder of its binaries",
    ),
]
PlatformOption = Annotated[
    Platform | None, typer.Option("--platform", help="Default: what --game-dir holds")
]


def schemagen_command(
    dump: Annotated[
        Path | None,
        typer.Option("--dump", dir_okay=False, help="Default: the one the local server wrote"),
    ] = None,
    server: ServerDir = None,
    platform: Annotated[
        Platform, typer.Option("--platform", help="The server the dump came from")
    ] = Platform.host(),
) -> None:
    """Regenerate the schema accessor layer from a dump."""
    project = current_project()
    manifest = read_json(project.root / SCHEMA_MANIFEST, "manifest")
    dump_file = dump or Cs2Server.open(server).root / SCHEMA_DUMP

    output = render_schema(read_json(dump_file, "dump"), manifest, platform)
    write_schema(project.root, output.files, platform)
    console.done(output.summary)


def gamedata_check_command(
    game_dir: GameDir = None,
    platform: PlatformOption = None,
    fix: Annotated[
        bool, typer.Option("--fix", help="Patch gamedata.jsonc where one moved offset explains it")
    ] = False,
) -> None:
    """Report which committed patterns no longer match the shipped binaries."""
    project = current_project()
    check = check_gamedata(project.root, game_dir, platform)
    console.step(f"gamedata {check.platform} (game build {check.game_build})")
    drifted = _print_drift(check.results)
    repaired = [result for result in check.results if result.status is PatternStatus.REPAIRED]

    if fix and repaired:
        write_repairs(project.root, check.text, repaired)
        console.done(f"Wrote {GAMEDATA_FILE} ({len(repaired)} patterns)")
        console.warn(
            "a unique match is not proof of behaviour: exercise each feature on a live server"
        )
        drifted -= len(repaired)
    elif repaired:
        console.info(f"{len(repaired)} of {drifted} drifted entries can be repaired with --fix")
    if drifted:
        raise typer.Exit(1)


def gamedata_fetch_command(
    platform: Annotated[Platform | None, typer.Option("--platform", help="Default: both")] = None,
    server: ServerDir = None,
) -> None:
    """Archive the current build's server binaries from Steam, for checks and old/new diffs.

    The archive is CS2_BUILD_ARCHIVE, else ~/.voltmod/cs2-builds.
    """
    root = archive_dir()
    for name in [platform] if platform else list(Platform):
        console.step(f"fetch {name}")
        target = download_build(root, name)
        console.item(str(target))
        if server and (build := archive_resolved(server.expanduser(), root, name)):
            console.note(f"kept the local server's resolved.{name}.json under build {build}")
        console.note(f"voltmod framework gamedata check --game-dir {target} --platform {name}")
    builds = archived_builds(root)
    if len(builds) > 1:
        console.info(f"Archived builds: {' '.join(builds)} (the previous one is {builds[-2]})")


def _print_drift(results: list[PatternResult]) -> int:
    """Print every pattern that no longer holds; return how many there are."""
    held = sum(result.status is PatternStatus.UNIQUE for result in results)
    console.note(f"{held}/{len(results)} patterns hold")
    for result in results:
        if result.status is PatternStatus.UNIQUE:
            continue
        console.labelled(f"{result.status.upper():9}", "yellow", f"{result.section}.{result.key}")
        for line in result.detail.splitlines():
            console.note(f"{'':9}{line}")
    return len(results) - held
