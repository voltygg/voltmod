"""Finding Visual Studio's C++ compiler and loading its environment on Windows."""

import os
import re
import shutil
import subprocess
from pathlib import Path

from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.toolchain.process import WINDOWS, put_tools_first_on_path, run


def msvc_version() -> str:
    """The newest installed cl version the way Conan spells it, such as `195`."""
    found = _query_visual_studio("-find", r"VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe")
    if not found:
        raise VoltmodError("no cl.exe found; install Visual Studio with the C++ tools")
    # cl prints its banner to stderr.
    banner = run(sorted(found)[-1], capture=True, check=False).stderr
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

    console.step(f"Loading MSVC environment ({visual_studio})")
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
    return run(vswhere, "-latest", "-products", "*", *args, capture=True).stdout.splitlines()
