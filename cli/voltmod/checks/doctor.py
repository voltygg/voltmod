"""Read-only checks for a VoltMod development environment."""

import shutil
import subprocess
import sys
from pathlib import Path

from .. import tools


class Report:
    """Collect and print doctor results."""

    def __init__(self) -> None:
        self.failures = 0
        self.warnings = 0

    def pass_(self, message: str) -> None:
        print(f"PASS  {message}")

    def warn(self, message: str) -> None:
        self.warnings += 1
        print(f"WARN  {message}")

    def fail(self, message: str) -> None:
        self.failures += 1
        print(f"FAIL  {message}")


def _check_tools(report: Report) -> None:
    for tool in ("cmake", "conan", "ninja"):
        try:
            version, actual = tools.tool_version(tool)
            minimum = tools.MINIMUM_VERSIONS.get(tool)
            if minimum and actual < minimum:
                required = ".".join(str(part) for part in minimum)
                report.fail(f"{tool}: {version}; VoltMod requires {required} or newer")
            else:
                report.pass_(f"{tool}: {version}")
        except SystemExit as exc:
            report.fail(f"{tool}: {exc}")


def _check_compiler(report: Report) -> None:
    if tools.WINDOWS:
        try:
            report.pass_(f"MSVC compiler: {tools.msvc_version()}")
        except (KeyError, OSError, RuntimeError, subprocess.SubprocessError, SystemExit) as exc:
            report.fail(f"MSVC compiler: {exc}")
        return

    compiler = next((name for name in ("g++", "clang++", "c++") if shutil.which(name)), None)
    if compiler is None:
        report.fail("C++ compiler: install GCC or Clang with C++23 support")
        return
    try:
        report.pass_(f"C++ compiler: {tools.tool_version(compiler)[0]}")
    except SystemExit as exc:
        report.fail(f"C++ compiler: {exc}")


def _check_project(report: Report, root: Path) -> None:
    for relative in ("CMakeLists.txt", "CMakePresets.json", "conanfile.py", "pyproject.toml"):
        path = root / relative
        if path.is_file():
            report.pass_(f"project file: {relative}")
        else:
            report.fail(f"project file missing: {relative}")

    if any(path.is_dir() for path in tools.profile_dirs(root)):
        report.pass_("Conan profiles are available")
    else:
        report.warn("Conan profiles are not installed yet; run `voltmod bootstrap`")

    try:
        result = tools.run_tool("conan", "remote", "list", capture=True, check=False)
        if result.returncode == 0 and f"{tools.CONAN_REMOTE}:" in result.stdout:
            report.pass_(f"Conan remote: {tools.CONAN_REMOTE}")
        else:
            report.warn(
                f"Conan remote '{tools.CONAN_REMOTE}' is not configured; "
                "run `voltmod bootstrap`"
            )
    except SystemExit as exc:
        report.fail(f"Conan remote check: {exc}")


def _check_server(report: Report, server_path: Path) -> None:
    csgo = server_path / "game/csgo"
    if not csgo.is_dir():
        report.fail(f"CS2 server: expected {csgo}")
        return
    report.pass_(f"CS2 server: {server_path}")

    binaries = (
        server_path / "game/bin/win64/cs2.exe",
        server_path / "game/bin/linuxsteamrt64/cs2",
    )
    if any(path.is_file() for path in binaries):
        report.pass_("CS2 dedicated-server executable found")
    else:
        report.fail("CS2 dedicated-server executable not found")

    metamod_binaries = (
        csgo / "addons/metamod/bin/win64/server.dll",
        csgo / "addons/metamod/bin/linuxsteamrt64/server.so",
    )
    if any(path.is_file() for path in metamod_binaries):
        report.pass_("Metamod installation found")
    else:
        report.warn("Metamod binary not found; install Metamod before loading plugins")


def run(root: Path, server_path: str = "") -> int:
    """Check tools, project files, and an optional local CS2 server."""
    report = Report()
    print(f"VoltMod doctor\nProject: {root.resolve()}\nPython: {sys.version.split()[0]}")
    _check_tools(report)
    _check_compiler(report)
    _check_project(report, root)
    if server_path:
        _check_server(report, Path(server_path).expanduser())
    else:
        report.warn("CS2 server check skipped; pass --server-path to include it")

    print(f"\nResult: {report.failures} failure(s), {report.warnings} warning(s)")
    return 1 if report.failures else 0
