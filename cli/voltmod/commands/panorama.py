"""The `voltmod panorama` commands: render, compile, publish, check and preview screens."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod.check_results import print_results
from voltmod.panorama.check import check_screens
from voltmod.panorama.compiler import compile_and_install, publish_screens
from voltmod.panorama.preview import open_in_browser, write_preview
from voltmod.panorama.render import (
    find_screen_owners,
    render_screens,
    screen_sources,
    select_owners,
)
from voltmod.project import Project

panorama_commands = typer.Typer(help="Render, compile, and publish Panorama screens.")

Owners = Annotated[
    list[str] | None,
    typer.Argument(
        metavar="[OWNER]...",
        help="Whose screens: a plugin name. Default: every plugin that ships some.",
    ),
]


@panorama_commands.command("render")
def render_command(
    owners: Owners = None,
    out: Annotated[
        Path | None,
        typer.Option("--out", help="Render into this directory instead of build/panorama"),
    ] = None,
) -> None:
    """Render panorama/screens/ into the build tree."""
    written = render_screens(Project.load().root, owners or [], out)
    print(f"Rendered {len(written)} file(s)")


@panorama_commands.command("compile")
def compile_command(
    owners: Owners = None,
    client_path: Annotated[
        str,
        typer.Option(
            "--client-path",
            help="CS2 *client* root, not the server (default: CS2_CLIENT_PATH, else via Steam)",
        ),
    ] = "",
    addon: Annotated[
        str, typer.Option("--addon", help="csgo_addons folder to compile through")
    ] = "voltmod",
    deploy: Annotated[
        bool,
        typer.Option(
            "--deploy/--no-deploy", help="Copy the compiled resources into the client's csgo/"
        ),
    ] = True,
) -> None:
    """Render, compile with the Workshop Tools, and install into your client."""
    project = Project.load()
    render_screens(project.root, owners or [])
    client_path = client_path or project.settings.client_path
    compile_and_install(project.root, owners or [], client_path, addon, deploy)


@panorama_commands.command("publish")
def publish_command(
    directory: Annotated[Path, typer.Argument(help="Addon content directory to copy into")],
    owners: Owners = None,
) -> None:
    """Render, then copy the panorama/ trees into an addon content directory."""
    root = Project.load().root
    render_screens(root, owners or [])
    count = publish_screens(root, owners or [], directory.expanduser())
    print(f"Published {count} file(s) into {directory}; point the Workshop Tools at it.")


@panorama_commands.command("check")
def check_command(owners: Owners = None) -> None:
    """Validate rendered screens against the rules the CS2 client enforces silently."""
    root = Project.load().root
    if print_results(check_screens(root, owners or [])):
        raise typer.Exit(1)
    selected = select_owners(find_screen_owners(root), owners or []).values()
    print(f"Checked {sum(len(screen_sources(owner)) for owner in selected)} screen(s)")


@panorama_commands.command("preview")
def preview_command(
    target: Annotated[str, typer.Argument(help="OWNER/SCREEN to preview")],
    open_browser: Annotated[
        bool, typer.Option("--open", help="Open the written HTML in a browser")
    ] = False,
) -> None:
    """Write a self-contained HTML approximation of a screen; no client needed."""
    out = write_preview(Project.load().root, target)
    if open_browser:
        open_in_browser(out)
    print(out)
