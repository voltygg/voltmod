"""The doctor, lint and format commands."""

import sys
from pathlib import Path
from typing import Annotated

import typer

from voltmod.checks.conventions import check_plugins
from voltmod.checks.doctor import run_checks
from voltmod.checks.results import exit_on_failure
from voltmod.project import Project
from voltmod.toolchain.clang_format import find_cpp_sources, format_cpp_files


def doctor_command(
    server_path: Annotated[
        str, typer.Option("--server-path", help="Optional CS2 server root to check")
    ] = "",
) -> None:
    """Check the local toolchain, the project, and an optional server."""
    project = Project.load()
    print(f"VoltMod doctor\nProject: {project.root.resolve()}\nPython: {sys.version.split()[0]}")
    exit_on_failure(run_checks(project, server_path))


def lint_command(
    root: Annotated[
        Path | None,
        typer.Argument(help="Repo whose plugins/ to check (default: the working directory)"),
    ] = None,
) -> None:
    """Check plugin sources for the VoltMod source conventions."""
    exit_on_failure(check_plugins(root or Project.load().root))
    print("Plugin sources hold.")


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
