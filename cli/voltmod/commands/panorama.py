"""The `voltmod panorama` commands: render, compile and check screens."""

from pathlib import Path
from typing import Annotated

import typer

from voltmod.check_results import print_results
from voltmod.panorama.check import check_screens
from voltmod.panorama.compiler import compile_and_install
from voltmod.panorama.render import render_screens, screen_owners, screen_sources
from voltmod.project import Project

panorama_commands = typer.Typer(help="Render, check and compile Panorama screens.")

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
    written = render_screens(Project.load().root, owners, out)
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
        str,
        typer.Option(
            "--addon", help="csgo_addons folder to compile into; the Workshop Manager uploads it"
        ),
    ] = "voltmod",
    deploy: Annotated[
        bool,
        typer.Option(
            "--deploy/--no-deploy", help="Copy the compiled resources into the client's csgo/"
        ),
    ] = True,
) -> None:
    """Check, render, compile with the Workshop Tools, and install into your client."""
    project = Project.load()
    if print_results(check_screens(project.root, owners)):
        raise typer.Exit(1)
    render_screens(project.root, owners)
    client_path = client_path or project.settings.client_path
    compile_and_install(project.root, owners, client_path, addon, deploy)


@panorama_commands.command("check")
def check_command(owners: Owners = None) -> None:
    """Validate rendered screens against the rules the CS2 client enforces silently."""
    root = Project.load().root
    if print_results(check_screens(root, owners)):
        raise typer.Exit(1)
    count = sum(len(screen_sources(owner)) for owner in screen_owners(root, owners))
    print(f"Checked {count} screen(s)")
