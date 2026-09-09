"""The `voltmod panorama` commands: render screens, compile them, publish them."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod import tools

from . import check as checker
from . import compile as compiler
from . import find_owners, select
from . import render as renderer

app = typer.Typer(help="Render, compile, and publish Panorama screens.")

ROOT = Path.cwd()
KIT_ROOT = tools.kit_root()

Owners = Annotated[
    list[str] | None,
    typer.Argument(
        metavar="[OWNER]...",
        help="Whose screens: a plugin name, or 'voltmod' for the framework's own. "
        "Default: every one found.",
    ),
]


@app.command("render")
def render_command(
    owners: Owners = None,
    out: Annotated[
        Path | None,
        typer.Option("--out", help="Render into this directory instead of build/panorama"),
    ] = None,
) -> None:
    """Render panorama/screens/ into the build tree."""
    written = renderer.render(ROOT, KIT_ROOT, owners or [], out)
    print(f"Rendered {len(written)} file(s)")


@app.command("compile")
def compile_command(
    owners: Owners = None,
    client_path: Annotated[
        str,
        typer.Option(
            "--client-path",
            envvar="CS2_CLIENT_PATH",
            help="CS2 *client* installation root (not the server). Found via Steam when unset.",
        ),
    ] = "",
    addon: Annotated[
        str,
        typer.Option("--addon", help="csgo_addons folder to compile through"),
    ] = "voltmod",
    deploy: Annotated[
        bool,
        typer.Option(
            "--deploy/--no-deploy",
            help="Copy the compiled resources into the client's own csgo/panorama",
        ),
    ] = True,
) -> None:
    """Render, compile with the Workshop Tools, and install into your client."""
    renderer.render(ROOT, KIT_ROOT, owners or [])
    compiler.install(ROOT, KIT_ROOT, owners or [], client_path, addon, deploy)


@app.command("publish")
def publish_command(
    directory: Annotated[Path, typer.Argument(help="Addon content directory to copy into")],
    owners: Owners = None,
) -> None:
    """Render, then copy the panorama/ trees into an addon content directory."""
    renderer.render(ROOT, KIT_ROOT, owners or [])
    count = compiler.publish(ROOT, KIT_ROOT, owners or [], directory.expanduser())
    print(f"Published {count} file(s) into {directory}; point the Workshop Tools at it.")


@app.command("check")
def check_command(owners: Owners = None) -> None:
    """Validate rendered screens against the rules the CS2 client enforces silently."""
    findings = checker.check(ROOT, KIT_ROOT, owners or [])
    if findings:
        for finding in findings:
            print(finding)
        raise typer.Exit(1)

    selected = select(find_owners(ROOT, KIT_ROOT), owners or [])
    count = sum(len(renderer.sources(owner)) for owner in selected.values())
    print(f"Checked {count} screen(s)")
