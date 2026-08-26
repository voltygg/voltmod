"""Unified build, scaffold, and package CLI for VoltMod projects."""

import os
from pathlib import Path
from typing import Annotated

import typer

from . import buildtools, doctor, init_project, localdev, modgraph, new_plugin, package

ROOT = Path.cwd()
KIT_ROOT = Path(__file__).resolve().parents[2]
CONFIG_SOURCE = "https://github.com/voltygg/voltmod.git"

app = typer.Typer(
    help="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    no_args_is_help=True,
)
app.add_typer(package.app, name="package")

ServerPath = Annotated[
    str,
    typer.Option("--server-path", envvar="CS2_SERVER_PATH", help="CS2 server installation root"),
]


@app.callback()
def _configure() -> None:
    """Apply local defaults from .env before Typer resolves any option."""
    localdev.load_env(ROOT)


def _exit_on_error(code: int) -> None:
    if code:
        raise typer.Exit(code)


@app.command()
def build(
    preset: Annotated[
        str | None,
        typer.Argument(help="CMake preset (default: release for this OS)"),
    ] = None,
    install_plugin: Annotated[
        str,
        typer.Option("--install", help="Install this plugin into the local CS2 server"),
    ] = "",
    start: Annotated[
        bool,
        typer.Option("--start", help="Launch the local CS2 server afterwards"),
    ] = False,
    server_path: ServerPath = "",
    option: Annotated[
        list[str] | None,
        typer.Option(
            "--option",
            "-o",
            metavar="NAME=VALUE",
            help="Pass through to `conan install` (repeatable)",
        ),
    ] = None,
) -> None:
    """Run Conan install and CMake build for one preset."""
    preset = preset or localdev.default_preset()

    # Fail on a bad plugin name or server path before spending a full build on it.
    if install_plugin:
        localdev.plugin_names(ROOT, install_plugin)
    if install_plugin or start:
        localdev.server_root(server_path)

    buildtools.build(
        ROOT,
        preset,
        run_tests=False,
        options=[item for value in option or [] for item in ("-o", value)],
    )

    if install_plugin:
        localdev.install(ROOT, server_path, install_plugin, preset)
    if start:
        _serve_from_env(server_path)


@app.command("test")
def test_command(
    preset: Annotated[
        str | None,
        typer.Argument(help="CMake preset (default: release for this OS)"),
    ] = None,
    filter_: Annotated[
        str,
        typer.Option("--filter", "-R", metavar="REGEX", help="Only run matching test cases"),
    ] = "",
) -> None:
    """Bring the build up to date, then run its tests."""
    buildtools.test(ROOT, preset or localdev.default_preset(), filter_=filter_)


def _serve_from_env(server_path: str) -> None:
    """Launch the server using .env defaults; `voltmod serve` takes explicit overrides."""
    localdev.serve(
        server_path,
        steamcmd_path=os.environ.get("STEAMCMD_PATH", ""),
        map_name=os.environ.get("CS2_MAP", "de_dust2"),
        gslt_token=os.environ.get("GSLT_TOKEN", ""),
        max_players=int(os.environ.get("CS2_MAX_PLAYERS", "16")),
        port=int(os.environ.get("CS2_PORT", "27015")),
        rcon_password=os.environ.get("RCON_PASSWORD", ""),
    )


@app.command("install")
def install_command(
    plugin: Annotated[
        str,
        typer.Argument(help="Plugin to install (default: every built plugin)"),
    ] = "",
    preset: Annotated[
        str | None,
        typer.Option("--preset", help="Build directory to install from"),
    ] = None,
    server_path: ServerPath = "",
) -> None:
    """Install already-built plugins into a local CS2 server."""
    localdev.install(ROOT, server_path, plugin, preset or localdev.default_preset())


@app.command()
def serve(
    server_path: ServerPath = "",
    steamcmd_path: Annotated[
        str,
        typer.Option("--steamcmd-path", envvar="STEAMCMD_PATH"),
    ] = "",
    map_: Annotated[str, typer.Option("--map", envvar="CS2_MAP")] = "de_dust2",
    port: Annotated[int, typer.Option("--port", envvar="CS2_PORT")] = 27015,
    max_players: Annotated[int, typer.Option("--max-players", envvar="CS2_MAX_PLAYERS")] = 16,
    gslt_token: Annotated[str, typer.Option("--gslt-token", envvar="GSLT_TOKEN")] = "",
    rcon_password: Annotated[str, typer.Option("--rcon-password", envvar="RCON_PASSWORD")] = "",
    check_update: Annotated[
        bool,
        typer.Option("--check-update", help="Refresh the server with SteamCMD first"),
    ] = False,
) -> None:
    """Run the local CS2 dedicated server in the foreground."""
    localdev.serve(
        server_path,
        steamcmd_path=steamcmd_path,
        map_name=map_,
        gslt_token=gslt_token,
        max_players=max_players,
        port=port,
        rcon_password=rcon_password,
        check_update=check_update,
    )


@app.command()
def bootstrap() -> None:
    """Install Conan profiles and the remote, then build."""
    print("==> [1/2] Installing Conan profiles and remotes")
    if (ROOT / "conan/profiles").is_dir():
        source = [str(ROOT / "conan")]
    else:
        source = [CONFIG_SOURCE, "-sf", "conan"]
    buildtools.run_tool("conan", "config", "install", *source)

    print("==> [2/2] Building with Conan + CMake")
    buildtools.build(ROOT, buildtools.default_preset())

    print("\nBootstrap complete: build/<preset>/plugins/")


@app.command("doctor")
def doctor_command(
    server_path: Annotated[
        str,
        typer.Option("--server-path", help="Optional CS2 server installation root"),
    ] = "",
) -> None:
    """Check the local toolchain, project, and optional server."""
    _exit_on_error(doctor.run(ROOT, server_path))


@app.command("format")
def format_command(
    dirs: Annotated[
        list[str] | None,
        typer.Argument(help="Directories to format (default: this repo's sources)"),
    ] = None,
) -> None:
    """Rewrite C++ sources in the pinned clang-format style."""
    selected = dirs or (["src", "include", "tests"] if ROOT == KIT_ROOT else ["plugins"])
    buildtools.format_sources(ROOT, selected)


@app.command("modgraph")
def modgraph_command(
    root: Annotated[
        Path,
        typer.Argument(help="Repo root (default: the working directory)"),
    ] = Path("."),
    plugins: Annotated[
        Path | None,
        typer.Option(
            "--plugins",
            help="Check a consumer repo's plugins/ for the source conventions instead of "
            "the framework's module layering",
        ),
    ] = None,
) -> None:
    """Check VoltMod's module layering, or a consumer's plugin sources."""
    if plugins is not None:
        _exit_on_error(modgraph.check_plugins(plugins))
        return
    _exit_on_error(modgraph.check(root))


@app.command("new-plugin")
def new_plugin_command(
    name: Annotated[
        str,
        typer.Argument(
            callback=new_plugin.kebab_case,
            help="Kebab-case name, e.g. fun-votes",
        ),
    ],
) -> None:
    """Stamp a plugin skeleton into plugins/<name>/."""
    _exit_on_error(new_plugin.create(name))


@app.command()
def init(
    name: Annotated[
        str,
        typer.Option(
            "--name",
            callback=new_plugin.kebab_case,
            help="Kebab-case project name (default: the directory name)",
        ),
    ] = ROOT.name,
    plugin: Annotated[
        str,
        typer.Option(
            "--plugin",
            callback=new_plugin.kebab_case,
            help="Kebab-case name for the first plugin",
        ),
    ] = "my-plugin",
) -> None:
    """Stamp a whole consumer project into the working directory."""
    _exit_on_error(init_project.create(name, plugin))


def main() -> None:
    app()
