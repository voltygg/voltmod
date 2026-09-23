"""Read-only checks of the toolchain, the project files, and an optional CS2 server."""

import json
import shutil
import subprocess
import urllib.request
from collections.abc import Iterator
from pathlib import Path

from voltmod.checks.results import CheckResult, Status
from voltmod.errors import VoltmodError
from voltmod.framework.gamedata import parse_gamedata, read_gamedata
from voltmod.platforms import Platform
from voltmod.project import Project
from voltmod.server.cs2_server import GAMEINFO, Cs2Server
from voltmod.server.install import HOST_VDF, host_binary
from voltmod.steam import CS2_APP
from voltmod.toolchain.conan import REMOTE, has_remote, profile_dirs
from voltmod.toolchain.msvc import msvc_version
from voltmod.toolchain.process import BUILD_TOOLS, WINDOWS, tool_version

PROJECT_FILES = ("CMakeLists.txt", "CMakePresets.json", "conanfile.py", "pyproject.toml")

UP_TO_DATE_CHECK = (
    f"https://api.steampowered.com/ISteamApps/UpToDateCheck/v1/?appid={CS2_APP}&version="
)


def run_checks(project: Project, server_path: str) -> Iterator[CheckResult]:
    yield from _check_tools()
    yield _check_compiler()
    yield from _check_project(project.root)
    if server_path:
        server = Cs2Server(Path(server_path).expanduser())
        yield from _check_server(server)
        if server.csgo.is_dir():
            yield from _check_game_build(project, server)
    else:
        yield CheckResult("CS2 server check skipped; pass --server-path to include it", Status.WARN)


def _passed(message: str) -> CheckResult:
    return CheckResult(message, Status.PASS)


def _check_tools() -> Iterator[CheckResult]:
    for tool in BUILD_TOOLS:
        try:
            yield _passed(f"{tool}: {tool_version(tool)}")
        except VoltmodError as error:
            yield CheckResult(f"{tool}: {error}")


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
        return _passed(f"C++ compiler: {tool_version(compiler)}")
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


def _check_server(server: Cs2Server) -> Iterator[CheckResult]:
    csgo = server.csgo
    if not csgo.is_dir():
        yield CheckResult(f"CS2 server: expected {csgo}")
        return
    yield _passed(f"CS2 server: {server.root}")

    if server.executable is not None:
        yield _passed("CS2 dedicated-server executable found")
    else:
        yield CheckResult("CS2 dedicated-server executable not found")

    metamod = (f"addons/metamod/bin/{p.bin_dir}/server{p.library_suffix}" for p in Platform)
    if any((csgo / path).is_file() for path in metamod):
        yield _passed("Metamod installation found")
    else:
        yield CheckResult(
            "Metamod binary not found; install Metamod before loading plugins", Status.WARN
        )

    if (server.root / GAMEINFO).is_file() and not server.has_metamod_search_path():
        yield CheckResult(
            "gameinfo.gi lost Metamod's search path (a CS2 update rewrites it); "
            "`voltmod serve` restores it",
            Status.WARN,
        )

    installed = any((csgo / host_binary(platform)).is_file() for platform in Platform)
    if installed and (csgo / HOST_VDF).is_file():
        yield _passed("VoltMod host installed")
    else:
        yield CheckResult(
            "VoltMod host not installed; run `voltmod install` before starting the server",
            Status.WARN,
        )


def _check_game_build(project: Project, server: Cs2Server) -> Iterator[CheckResult]:
    build = server.build
    yield _check_up_to_date(build, server.patch_version)
    if not project.is_framework:
        return
    gamedata = parse_gamedata(read_gamedata(project.root))
    checked = gamedata["build"]["server"]
    if checked == build:
        yield _passed(f"gamedata was checked on this server's build {build}")
    else:
        yield CheckResult(
            f"gamedata was checked on build {checked}, the server runs {build}; "
            "see docs/sdk/gamedata.md",
            Status.WARN,
        )


def _check_up_to_date(build: str, version: str) -> CheckResult:
    try:
        with urllib.request.urlopen(UP_TO_DATE_CHECK + version, timeout=10) as response:
            answer = json.load(response)["response"]
    except (OSError, ValueError, KeyError) as error:
        return CheckResult(f"Steam up-to-date check failed: {error}", Status.WARN)
    if answer.get("up_to_date"):
        return _passed(f"CS2 server {version} ({build}) is the current build")
    required = answer.get("message", "Steam requires a newer build")
    return CheckResult(
        f"CS2 server {version} is out of date ({required}); update it, then follow "
        "docs/sdk/gamedata.md",
        Status.WARN,
    )
