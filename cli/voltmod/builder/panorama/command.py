"""The `voltmod panorama` commands: render screens, compile them, publish them."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod import tools

from . import compile as compiler
from . import preview as previewer
from . import screens

app = typer.Typer(help="Render, compile, and publish Panorama screens.")

ROOT = Path.cwd()
KIT_ROOT = tools.kit_root()

Owners = Annotated[
    list[str] | None,
    typer.Argument(
        metavar="[OWNER]...",
        help="Whose screens: a plugin name. Default: every plugin that ships some.",
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
    written = screens.render(ROOT, KIT_ROOT, owners or [], out)
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
    screens.render(ROOT, KIT_ROOT, owners or [])
    compiler.install(ROOT, owners or [], client_path, addon, deploy)


@app.command("publish")
def publish_command(
    directory: Annotated[Path, typer.Argument(help="Addon content directory to copy into")],
    owners: Owners = None,
) -> None:
    """Render, then copy the panorama/ trees into an addon content directory."""
    screens.render(ROOT, KIT_ROOT, owners or [])
    count = compiler.publish(ROOT, owners or [], directory.expanduser())
    print(f"Published {count} file(s) into {directory}; point the Workshop Tools at it.")


@app.command("check")
def check_command(owners: Owners = None) -> None:
    """Validate rendered screens against the rules the CS2 client enforces silently."""
    findings = screens.check(ROOT, KIT_ROOT, owners or [])
    if findings:
        for finding in findings:
            print(finding)
        raise typer.Exit(1)

    selected = screens.select(screens.find_owners(ROOT), owners or [])
    count = sum(len(screens.sources(owner)) for owner in selected.values())
    print(f"Checked {count} screen(s)")


@app.command("preview")
def preview_command(
    target: Annotated[str, typer.Argument(help="OWNER/SCREEN to preview")],
    open_browser: Annotated[
        bool, typer.Option("--open", help="Open the written HTML in a browser")
    ] = False,
) -> None:
    """Write a self-contained HTML approximation of a screen; no client needed."""
    out = previewer.preview(ROOT, KIT_ROOT, target)
    if open_browser:
        previewer.open_in_browser(out)
    print(out)
