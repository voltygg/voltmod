import os
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

from voltmod.errors import VoltmodError

WINDOWS = sys.platform == "win32"

# cmake, ctest, conan, ninja and clang-format are voltmod dependencies, installed here.
TOOLS_DIR = Path(sysconfig.get_path("scripts"))

BUILD_TOOLS = ("cmake", "conan", "ninja")


def put_tools_first_on_path() -> None:
    """Make child processes (conan calling cmake, cmake calling ninja) use the pinned tools."""
    os.environ["PATH"] = f"{TOOLS_DIR}{os.pathsep}{os.environ.get('PATH', '')}"


def find_tool(tool: str) -> str:
    found = shutil.which(tool, path=str(TOOLS_DIR)) or shutil.which(tool)
    if not found:
        raise VoltmodError(f"'{tool}' was not found; run `uv sync` to install voltmod's tools")
    return found


def run(
    *command: str | Path,
    capture: bool = False,
    check: bool = True,
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run any program; `run_tool` is for the pinned tools."""
    arguments = [str(part) for part in command]
    return subprocess.run(arguments, check=check, text=True, capture_output=capture, cwd=cwd)


def run_tool(
    tool: str,
    *args: str | Path,
    capture: bool = False,
    check: bool = True,
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    return run(find_tool(tool), *args, capture=capture, check=check, cwd=cwd)


def tool_output(tool: str, *args: str | Path) -> str:
    return run_tool(tool, *args, capture=True).stdout


def tool_version(tool: str) -> str:
    """A tool's first `--version` line."""
    result = run_tool(tool, "--version", capture=True, check=False)
    lines = (result.stdout or result.stderr).strip().splitlines()
    if result.returncode != 0 or not lines:
        raise VoltmodError(f"`{tool} --version` failed")
    return lines[0]
