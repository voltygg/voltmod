from pathlib import Path
from typing import Annotated

import typer

from voltmod import console
from voltmod.checks.results import exit_if_failed
from voltmod.errors import VoltmodError
from voltmod.options import current_project
from voltmod.panorama.check import check_screens
from voltmod.panorama.compiler import AddonDirs, compile_resources, install_into_client, stage
from voltmod.panorama.render import render_screens
from voltmod.panorama.sources import panorama_plugins, screen_templates
from voltmod.steam import find_client
from voltmod.toolchain.process import WINDOWS

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
    if not WINDOWS:
        raise VoltmodError("the CS2 Workshop Tools are Windows only; compile the layouts there")
    root = current_project().root
    exit_if_failed(check_screens(root, plugins))
    render_screens(root, plugins)

    dirs = AddonDirs.of(find_client(client), addon)
    console.step(f"Compiling into csgo_addons/{addon} of {dirs.client}")
    staged = stage(root, plugins, dirs)
    if staged:
        sources = sum(len(plugin.files) for plugin in staged)
        names = ", ".join(plugin.name for plugin in staged)
        console.step(f"Staged {sources} source(s) from {names}")
        compile_resources(dirs, staged)

    if not deploy:
        console.done(f"Compiled into {dirs.compiled}; not installed")
        return
    installed = install_into_client(dirs, staged)
    console.done(f"Installed {installed} resource(s). Reconnect to pick them up.")


def check_command(plugins: Plugins = None) -> None:
    """Validate rendered screens against the rules the CS2 client enforces silently."""
    root = current_project().root
    exit_if_failed(check_screens(root, plugins))
    count = sum(len(screen_templates(plugin)) for plugin in panorama_plugins(root, plugins))
    console.done(f"Checked {count} screen(s)")
