"""Unified build, scaffold, and package CLI for VoltMod projects."""

from pathlib import Path
from typing import Annotated

import typer

from . import buildtools, doctor, init_project, modgraph, new_plugin, package

ROOT = Path.cwd()
KIT_ROOT = Path(__file__).resolve().parents[2]
CONFIG_SOURCE = "https://github.com/voltygg/voltmod.git"

app = typer.Typer(
    help="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    no_args_is_help=True,
)
app.add_typer(package.app, name="package")


def _exit_on_error(code: int) -> None:
    if code:
        raise typer.Exit(code)


@app.command()
def build(
    preset: Annotated[
        str | None,
        typer.Argument(help="CMake preset (default: release for this OS)"),
    ] = None,
    no_test: Annotated[
        bool,
        typer.Option("--no-test", help="Skip ctest, so CI can report tests separately"),
    ] = False,
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
    options = [item for value in option or [] for item in ("-o", value)]
    buildtools.build(
        ROOT,
        preset or buildtools.default_preset(),
        run_tests=not no_test,
        options=options,
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
    check: Annotated[
        bool,
        typer.Option("--check", help="Report diffs instead of writing them"),
    ] = False,
) -> None:
    """Run the pinned clang-format over C++ sources."""
    selected = dirs or (["src", "include", "tests"] if ROOT == KIT_ROOT else ["plugins"])
    buildtools.format_sources(ROOT, selected, check=check)


@app.command("modgraph")
def modgraph_command(
    root: Annotated[
        Path,
        typer.Argument(help="Repo root (default: the working directory)"),
    ] = Path("."),
) -> None:
    """Check VoltMod's module layering."""
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
