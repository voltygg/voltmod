import sys
from pathlib import Path
from typing import Annotated

import typer

from voltmod import console
from voltmod.checks.conventions import check_plugins
from voltmod.checks.doctor import run_checks
from voltmod.checks.results import exit_if_failed
from voltmod.framework.layering import check_layering
from voltmod.project import Project
from voltmod.toolchain.clang_format import find_cpp_sources, format_cpp_files


def doctor_command(
    server: Annotated[
        Path | None, typer.Option("--server", file_okay=False, help="A CS2 server root to check")
    ] = None,
) -> None:
    """Check the local toolchain, the project, and an optional server."""
    project = Project.load()
    console.step(f"VoltMod doctor for {project.root.resolve()}, Python {sys.version.split()[0]}")
    exit_if_failed(run_checks(project, server))


def lint_command() -> None:
    """Check C++ sources: plugin conventions, or the framework's layering in its checkout."""
    project = Project.load()
    if not project.is_framework:
        exit_if_failed(check_plugins(project.root))
        console.done("Plugin sources hold.")
        return

    dependencies, results = check_layering(project.root)
    for module, used in dependencies.items():
        console.info(f"{module:10} -> {' '.join(sorted(used)) or '(none)'}")
    exit_if_failed(results)
    console.done("Layering holds.")


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
        console.info("No C++ sources found.")
        return
    format_cpp_files(files)
    console.done(f"clang-format formatted {len(files)} file(s).")
