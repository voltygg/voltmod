"""The build, test, format and bootstrap commands."""

from typing import Annotated

import typer

from voltmod.build import bootstrap, build, run_tests
from voltmod.cpp_sources import find_cpp_sources, format_cpp_files
from voltmod.cs2_install import find_server
from voltmod.project import Project
from voltmod.server import install_plugins, run_server

build_commands = typer.Typer()

PresetArgument = Annotated[
    str | None,
    typer.Argument(help="CMake preset (default: CS2_BUILD_PRESET, else release for this OS)"),
]


@build_commands.command("build")
def build_command(
    preset: PresetArgument = None,
    install: Annotated[
        str, typer.Option("--install", help="Install this plugin into the local CS2 server")
    ] = "",
    start: Annotated[
        bool, typer.Option("--start", help="Launch the local CS2 server afterwards")
    ] = False,
    server_path: Annotated[
        str, typer.Option("--server-path", help="CS2 server root (default: CS2_SERVER_PATH)")
    ] = "",
    option: Annotated[
        list[str] | None,
        typer.Option(
            "--option", "-o", metavar="NAME=VALUE", help="Pass through to `conan install`"
        ),
    ] = None,
    no_lockfile: Annotated[
        bool,
        typer.Option("--no-lockfile", help="Resolve without conan.lock, as after an SDK bump"),
    ] = False,
    relock: Annotated[
        bool,
        typer.Option(
            "--relock",
            help="Export the editable voltmod checkout as a package, pin it in conan.lock, and "
            "drop the editable, so the build matches what CI resolves",
        ),
    ] = False,
) -> None:
    """Run Conan install and the CMake build for one preset."""
    project = Project.load()
    preset = project.resolve_preset(preset)
    server_path = server_path or project.settings.server_path

    # Fail on a bad plugin name or server path before spending a whole build on it.
    if install:
        project.plugin_names(install)
    if install or start:
        find_server(server_path)

    conan_options = [arg for value in option or [] for arg in ("-o", value)]
    build(project, preset, conan_options=conan_options, use_lockfile=not no_lockfile, relock=relock)

    if install:
        install_plugins(project, server_path, install, preset)
    if start:
        run_server(project.settings.with_options(server_path=server_path))


@build_commands.command("test")
def test_command(
    preset: PresetArgument = None,
    name_filter: Annotated[
        str, typer.Option("--filter", "-R", metavar="REGEX", help="Only run matching test cases")
    ] = "",
) -> None:
    """Bring the build up to date, then run its tests."""
    project = Project.load()
    run_tests(project, project.resolve_preset(preset), name_filter)


@build_commands.command("format")
def format_command(
    dirs: Annotated[
        list[str] | None,
        typer.Argument(help="Directories to format (default: this repo's C++ sources)"),
    ] = None,
) -> None:
    """Rewrite C++ sources in the pinned clang-format style."""
    project = Project.load()
    files = find_cpp_sources(project.root, dirs or project.cpp_source_dirs)
    if not files:
        print("No C++ sources found.")
        return
    format_cpp_files(files)
    print(f"clang-format formatted {len(files)} file(s).")


@build_commands.command("bootstrap")
def bootstrap_command() -> None:
    """Install the Conan profiles and remote, then build."""
    bootstrap(Project.load())
