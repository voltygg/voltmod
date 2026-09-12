"""CLI commands for checking and repairing gamedata signatures."""

import json
import re
from pathlib import Path
from typing import Annotated, Any

import typer

from voltmod.tools import abort, framework_root

from . import document, resolve
from .image import Modules, detect_platform

app = typer.Typer(help="Check and repair gamedata against the shipped game binaries.")

ROOT = Path.cwd()
GAMEDATA = Path("gamedata/gamedata.jsonc")
BASELINE = Path("schema/server.json")

GameDir = Annotated[
    str,
    typer.Option(
        "--game-dir", envvar="CS2_SERVER_PATH", help="CS2 install, or a folder of modules"
    ),
]
Platform = Annotated[
    str, typer.Option("--platform", help="windows or linux; defaults to what --game-dir holds")
]


def _framework() -> Path:
    """Find the framework checkout from the framework or a consumer repository."""
    for candidate in (ROOT, framework_root(), ROOT / "vendor/voltmod"):
        if (candidate / GAMEDATA).is_file():
            return candidate
    abort(f"no {GAMEDATA} in {ROOT} or {framework_root()}")


def _game_build(game_dir: Path) -> str:
    """Read the game's build number, which the framework also stamps on schema dumps."""
    inf = game_dir / "game/csgo/steam.inf"
    if not inf.is_file():
        return "unknown"
    found = re.search(r"ServerVersion=(\S+)", inf.read_text(encoding="utf-8", errors="replace"))
    return found.group(1) if found else "unknown"


def _schema(repo: Path) -> dict[str, Any]:
    """Load the committed baseline used to name a repaired displacement."""
    path = repo / BASELINE
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}


def _collect(game_dir: str, platform: str) -> tuple[Path, str, list[resolve.Finding]]:
    if not game_dir:
        abort("no game directory; set CS2_SERVER_PATH in .env or pass --game-dir")

    root = Path(game_dir).expanduser()
    if not root.is_dir():
        abort(f"no game directory at {root}")

    repo = _framework()
    text, parsed = document.read(repo / GAMEDATA)
    modules = Modules(root, platform or detect_platform(root))
    print(f"==> gamedata {modules.platform} (game build {_game_build(root)})")
    return repo, text, resolve.run(parsed, modules, _schema(repo))


def _report(findings: list[resolve.Finding]) -> int:
    """Print drift details and return the number of non-holding entries."""
    held = sum(finding.status == "holds" for finding in findings)
    print(f"    {held}/{len(findings)} signatures hold")
    for finding in findings:
        if finding.status == "holds":
            continue
        print(f"    {finding.status.upper():9} signatures.{finding.key}")
        for line in finding.detail.splitlines():
            print(f"{'':14}{line}")
    return len(findings) - held


@app.command()
def check(game_dir: GameDir = "", platform: Platform = "") -> None:
    """Report which committed signatures no longer match the shipped binaries."""
    _, _, findings = _collect(game_dir, platform)
    drifted = _report(findings)
    if drifted:
        print(f"{drifted} entries drifted; repair them with: voltmod gamedata resolve --write")
        raise typer.Exit(1)


@app.command(name="resolve")
def resolve_command(
    game_dir: GameDir = "",
    platform: Platform = "",
    write: Annotated[bool, typer.Option("--write", help="Patch gamedata.jsonc in place.")] = False,
) -> None:
    """Repair the signatures that drifted, leaving every entry that still matches alone."""
    repo, text, findings = _collect(game_dir, platform)
    drifted = _report(findings)

    repaired = [finding for finding in findings if finding.status == "repaired"]
    if not write:
        if repaired:
            print(f"{len(repaired)} entries would be rewritten (pass --write)")
        raise typer.Exit(1 if drifted else 0)

    for finding in repaired:
        text = document.set_pattern(text, finding.key, finding.old, finding.new)
    if repaired:
        document.write(repo / GAMEDATA, text)
        print(f"wrote {GAMEDATA} ({len(repaired)} patterns)")

    print("A unique match is not proof of behaviour: exercise each feature on a live server.")
    if drifted - len(repaired):
        raise typer.Exit(1)
