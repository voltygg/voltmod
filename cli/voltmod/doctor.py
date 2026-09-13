"""Read-only checks of the toolchain, the project files, and an optional CS2 server."""

import shutil
import subprocess
from collections.abc import Iterator
from pathlib import Path

from voltmod.check_results import CheckResult, Status
from voltmod.conan import REMOTE, has_remote, profile_dirs
from voltmod.cs2_install import METAMOD_BINARIES, server_executable
from voltmod.errors import VoltmodError
from voltmod.process import BUILD_TOOLS, WINDOWS, check_tool_version, msvc_version, tool_version
from voltmod.project import Project

PROJECT_FILES = ("CMakeLists.txt", "CMakePresets.json", "conanfile.py", "pyproject.toml")


def run_checks(project: Project, server_path: str) -> Iterator[CheckResult]:
    yield from _check_tools()
    yield _check_compiler()
    yield from _check_project(project.root)
    if server_path:
        yield from _check_server(Path(server_path).expanduser())
    else:
        yield CheckResult("CS2 server check skipped; pass --server-path to include it", Status.WARN)


def _passed(message: str) -> CheckResult:
    return CheckResult(message, Status.PASS)


def _check_tools() -> Iterator[CheckResult]:
    for tool in BUILD_TOOLS:
        try:
            banner, problem = check_tool_version(tool)
        except VoltmodError as error:
            yield CheckResult(f"{tool}: {error}")
            continue
        yield CheckResult(f"{tool}: {problem}") if problem else _passed(f"{tool}: {banner}")


def _check_compiler() -> CheckResult:
    if WINDOWS:
        try:
            return _passed(f"MSVC compiler: {msvc_version()}")
        except (VoltmodError, OSError, subprocess.SubprocessError) as error:
            return CheckResult(f"MSVC compiler: {error}")

    compiler = next((name for name in ("g++", "clang++", "c++") if shutil.which(name)), None)
    if compiler is None:
        return CheckResult("C++ compiler: install GCC or Clang with C++23 support")
    try:
        return _passed(f"C++ compiler: {tool_version(compiler)[0]}")
    except VoltmodError as error:
        return CheckResult(f"C++ compiler: {error}")


def _check_project(root: Path) -> Iterator[CheckResult]:
    for name in PROJECT_FILES:
        if (root / name).is_file():
            yield _passed(f"project file: {name}")
        else:
            yield CheckResult(f"project file missing: {name}")

    if any(path.is_dir() for path in profile_dirs(root)):
        yield _passed("Conan profiles are available")
    else:
        yield CheckResult(
            "Conan profiles are not installed yet; run `voltmod bootstrap`", Status.WARN
        )

    try:
        if has_remote():
            yield _passed(f"Conan remote: {REMOTE}")
        else:
            yield CheckResult(
                f"Conan remote '{REMOTE}' is not configured; run `voltmod bootstrap`", Status.WARN
            )
    except VoltmodError as error:
        yield CheckResult(f"Conan remote check: {error}")


def _check_server(server: Path) -> Iterator[CheckResult]:
    if not (server / "game/csgo").is_dir():
        yield CheckResult(f"CS2 server: expected {server / 'game/csgo'}")
        return
    yield _passed(f"CS2 server: {server}")

    if server_executable(server) is not None:
        yield _passed("CS2 dedicated-server executable found")
    else:
        yield CheckResult("CS2 dedicated-server executable not found")

    if any((server / path).is_file() for path in METAMOD_BINARIES):
        yield _passed("Metamod installation found")
    else:
        yield CheckResult(
            "Metamod binary not found; install Metamod before loading plugins", Status.WARN
        )
