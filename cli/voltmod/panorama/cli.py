from pathlib import Path
from typing import Annotated

import typer

from voltmod import console
from voltmod.checks.results import exit_on_failure
from voltmod.options import current_project
from voltmod.panorama.check import check_screens
from voltmod.panorama.compiler import compile_and_install
from voltmod.panorama.render import render_screens
from voltmod.panorama.sources import screen_owners, screen_sources

Plugins = Annotated[
    list[str] | None,
    typer.Argument(
        metavar="[PLUGIN]...", help="Default: every plugin that ships screens", show_default=False
    ),
]


def render_command(
    plugins: Plugins = None,
    out: Annotated[
        Path | None,
        typer.Option("--out", help="Render into this directory instead of build/panorama"),
    ] = None,
) -> None:
    """Render panorama/screens/ into the build tree."""
    written = render_screens(current_project().root, plugins, out)
    console.done(f"Rendered {len(written)} file(s)")


def compile_command(
    plugins: Plugins = None,
    client: Annotated[
        Path | None,
        typer.Option(
            "--client",
            envvar="CS2_CLIENT_PATH",
            file_okay=False,
            help="CS2 *client* root, not the server (default: found through Steam)",
        ),
    ] = None,
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
    root = current_project().root
    exit_on_failure(check_screens(root, plugins))
    render_screens(root, plugins)
    compile_and_install(root, plugins, client, addon, deploy)


def check_command(plugins: Plugins = None) -> None:
    """Validate rendered screens against the rules the CS2 client enforces silently."""
    root = current_project().root
    exit_on_failure(check_screens(root, plugins))
    count = sum(len(screen_sources(owner)) for owner in screen_owners(root, plugins))
    console.done(f"Checked {count} screen(s)")
