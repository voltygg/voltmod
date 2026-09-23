from typing import Annotated

import typer

from voltmod.options import Preset
from voltmod.project import Project, default_preset
from voltmod.toolchain.build import bootstrap, build, run_tests


def build_command(
    preset: Preset = default_preset(),
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
    conan_options = [arg for value in option or [] for arg in ("-o", value)]
    build(
        Project.load(),
        preset,
        conan_options=conan_options,
        use_lockfile=not no_lockfile,
        relock=relock,
    )


def test_command(
    preset: Preset = default_preset(),
    name_filter: Annotated[
        str, typer.Option("--filter", "-R", metavar="REGEX", help="Only run matching test cases")
    ] = "",
) -> None:
    """Bring the build up to date, then run its tests."""
    run_tests(Project.load(), preset, name_filter)


def bootstrap_command(preset: Preset = default_preset()) -> None:
    """Install the Conan profiles and remote, then build."""
    bootstrap(Project.load(), preset)
