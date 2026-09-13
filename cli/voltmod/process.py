"""Running the build tools installed beside voltmod, and loading MSVC on Windows."""

import os
import re
import shutil
import subprocess
import sys
import sysconfig
from collections.abc import Callable
from pathlib import Path

from voltmod.errors import VoltmodError

WINDOWS = sys.platform == "win32"

# cmake, ctest, conan, ninja and clang-format are voltmod dependencies, installed here.
TOOLS_DIR = Path(sysconfig.get_path("scripts"))

# Both `voltmod build` and `voltmod doctor` judge a toolchain against these.
BUILD_TOOLS = ("cmake", "conan", "ninja")
MINIMUM_VERSIONS = {"cmake": (4, 3, 4), "conan": (2, 29, 1)}


def put_tools_first_on_path() -> None:
    """Make child processes (conan calling cmake, cmake calling ninja) use the pinned tools."""
    os.environ["PATH"] = f"{TOOLS_DIR}{os.pathsep}{os.environ.get('PATH', '')}"


def run_with_error_messages(main: Callable[[], object]) -> None:
    """Run @p main, reporting a VoltmodError or a failed tool as one line and exit code 1."""
    put_tools_first_on_path()
    try:
        main()
    except VoltmodError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as error:
        command = error.cmd if isinstance(error.cmd, str) else " ".join(map(str, error.cmd))
        print(f"error: `{command}` exited with {error.returncode}", file=sys.stderr)
        sys.exit(1)


def find_tool(tool: str) -> str:
    found = shutil.which(tool, path=str(TOOLS_DIR)) or shutil.which(tool)
    if not found:
        raise VoltmodError(f"'{tool}' was not found; run `uv sync` to install voltmod's tools")
    return found


def run_tool(
    tool: str,
    *args: str,
    capture: bool = False,
    check: bool = True,
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [find_tool(tool), *args]
    return subprocess.run(command, check=check, text=True, capture_output=capture, cwd=cwd)


def tool_version(tool: str) -> tuple[str, tuple[int, ...]]:
    """A tool's first `--version` line, and the version numbers in it (empty when it has none)."""
    result = run_tool(tool, "--version", capture=True, check=False)
    lines = (result.stdout or result.stderr).strip().splitlines()
    if result.returncode != 0 or not lines:
        raise VoltmodError(f"`{tool} --version` failed")
    match = re.search(r"\b\d+(?:\.\d+)+", lines[0])
    return lines[0], tuple(map(int, match.group().split("."))) if match else ()


def check_tool_version(tool: str) -> tuple[str, str]:
    """A tool's version line, and why it is too old ("" when it is new enough)."""
    banner, actual = tool_version(tool)
    minimum = MINIMUM_VERSIONS.get(tool)
    if minimum and actual < minimum:
        return banner, f"{banner}; voltmod requires {'.'.join(map(str, minimum))} or newer"
    return banner, ""


def require_build_tools() -> None:
    for tool in BUILD_TOOLS:
        _, problem = check_tool_version(tool)
        if problem:
            raise VoltmodError(f"{tool}: {problem}")


def msvc_version() -> str:
    """The newest installed cl version the way Conan spells it, such as `195`."""
    found = _query_visual_studio("-find", r"VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe")
    if not found:
        raise VoltmodError("no cl.exe found; install Visual Studio with the C++ tools")
    # cl prints its banner to stderr.
    banner = subprocess.run([sorted(found)[-1]], text=True, capture_output=True).stderr
    match = re.search(r"Version (\d+)\.(\d+)", banner)
    if not match:
        raise VoltmodError("could not read the cl version banner")
    version = f"{match.group(1)}{match.group(2)[0]}"
    if int(version) < 193:
        raise VoltmodError(f"cl {version} predates C++23 support")
    return version


def load_msvc_environment() -> None:
    """Import vcvars64 into os.environ on Windows, unless cl is already on PATH."""
    if not WINDOWS or shutil.which("cl"):
        return

    found = _query_visual_studio(
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property",
        "installationPath",
    )
    visual_studio = found[0].strip() if found else ""
    vcvars = Path(visual_studio) / "VC/Auxiliary/Build/vcvars64.bat"
    if not visual_studio or not vcvars.is_file():
        raise VoltmodError("vcvars64.bat not found; install the VC++ x64 toolset")

    print(f"==> Loading MSVC environment ({visual_studio})")
    # A string keeps the quoted vcvars path intact; list2cmdline does not.
    command = f'cmd /c "{vcvars}" >nul && set'
    output = subprocess.run(command, check=True, text=True, capture_output=True).stdout
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key and " " not in key:
            os.environ[key] = value

    if not shutil.which("cl"):
        raise VoltmodError("cl is still not on PATH after vcvars64.bat")
    # vcvars puts Visual Studio's own cmake and ninja first.
    put_tools_first_on_path()


def _query_visual_studio(*args: str) -> list[str]:
    """Ask vswhere about the newest Visual Studio install; one line per result."""
    program_files = os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        raise VoltmodError("vswhere not found; install Visual Studio with the C++ tools")
    query = [str(vswhere), "-latest", "-products", "*", *args]
    return subprocess.run(query, check=True, text=True, capture_output=True).stdout.splitlines()
