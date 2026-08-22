"""Shared build helpers for cs2-kit and projects that vendor it (e.g. cs2-plugins).

Import, don't execute. This is the single source of truth; consuming repos add this
directory to sys.path and call build().
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn

WINDOWS = sys.platform == "win32"
CPP_EXTS = (".cpp", ".hpp")
# Well under the 32767-character Windows command-line cap, leaving room for the
# resolved clang-format path and its flags.
MAX_COMMAND_LINE = 24000


def die(message: str) -> NoReturn:
    """Exit with an error message."""
    raise SystemExit(f"ERROR: {message}")


def default_preset() -> str:
    """Pick the release preset for the current OS."""
    return "windows-msvc-release" if WINDOWS else "linux-steamrt-release"


def run_tool(tool: str, *args: str) -> subprocess.CompletedProcess[bytes]:
    """Run a build tool, preferring the project venv, then `uv run`, then PATH."""
    for root in filter(None, (os.environ.get("VIRTUAL_ENV"), ".venv")):
        bindir = Path(root) / ("Scripts" if WINDOWS else "bin")
        exe = shutil.which(tool, path=str(bindir))
        if exe:
            env = {**os.environ, "PATH": f"{bindir}{os.pathsep}{os.environ['PATH']}"}
            return subprocess.run([exe, *args], check=True, env=env)

    if shutil.which("uv"):
        return subprocess.run(["uv", "run", tool, *args], check=True)
    if shutil.which(tool):
        return subprocess.run([tool, *args], check=True)
    die(
        f"'{tool}' was not found on PATH. Install CMake 4.3.4+, Conan 2.29.1+, and "
        "Ninja, or install uv and run `uv sync`."
    )


def _chunk_by_length(files: list[str], budget: int) -> list[list[str]]:
    """Split `files` into batches whose joined length stays under `budget` characters."""
    batches: list[list[str]] = [[]]
    used = 0
    for f in files:
        cost = len(f) + 3  # quotes and a separating space
        if batches[-1] and used + cost > budget:
            batches.append([])
            used = 0
        batches[-1].append(f)
        used += cost
    return [b for b in batches if b]


def format_sources(repo_root: Path, dirs: list[str], *, check: bool) -> None:
    """Run the pinned clang-format over C++ sources under dirs (relative to repo_root).

    rglob only descends the listed dirs, so nested SDK submodules under vendor/ are
    never touched. --check leaves files untouched and exits non-zero on any diff.
    """
    files = sorted(
        str(p)
        for d in dirs
        if (repo_root / d).is_dir()
        for ext in CPP_EXTS
        for p in (repo_root / d).rglob(f"*{ext}")
    )
    if not files:
        print("No C++ sources found.")
        return
    args = ["--dry-run", "--Werror"] if check else ["-i"]

    # Windows caps a command line at 32767 characters, which this repo's file list
    # exceeds - CreateProcess then fails with WinError 206 instead of formatting
    # anything. Batch on POSIX too so both platforms exercise the same path.
    failure = 0
    for batch in _chunk_by_length(files, MAX_COMMAND_LINE):
        try:
            run_tool("clang-format", *args, *batch)
        except subprocess.CalledProcessError as e:
            # Keep going: --check should report every offending file, not just the
            # first batch that happens to contain one.
            failure = failure or e.returncode
    if failure:
        raise SystemExit(failure)
    print(f"clang-format {'checked' if check else 'formatted'} {len(files)} file(s).")


def _tool_version(tool: str, prefix: str) -> tuple[int, ...]:
    """Read `<tool> --version` and parse the dotted number after `prefix`."""
    out = subprocess.run(
        [tool, "--version"], check=True, text=True, capture_output=True
    ).stdout
    for token in out.replace(prefix, " ").split():
        if token[:1].isdigit():
            return tuple(int(p) for p in token.split(".") if p.isdigit())
    return ()


def require_build_tools() -> None:
    """Verify Ninja is present and CMake/Conan meet the project minimums."""
    run_tool("ninja", "--version")
    for tool, prefix, minimum in (
        ("cmake", "cmake version", (4, 3, 4)),
        ("conan", "Conan version", (2, 29, 1)),
    ):
        actual = _tool_version(tool, prefix)
        if actual < minimum:
            want = ".".join(map(str, minimum))
            got = ".".join(map(str, actual)) or "unknown"
            die(f"{tool} {want} or newer is required, found {got}.")


def ensure_msvc_env() -> None:
    """Import the MSVC toolchain (cl + INCLUDE/LIB) into os.environ on Windows."""
    if not WINDOWS or shutil.which("cl"):
        return

    vswhere = Path(os.environ["ProgramFiles(x86)"]) / (
        "Microsoft Visual Studio/Installer/vswhere.exe"
    )
    if not vswhere.is_file():
        die("vswhere not found; install Visual Studio with C++ tools.")

    vs_path = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        check=True, text=True, capture_output=True,
    ).stdout.strip()
    vcvars = Path(vs_path) / "VC/Auxiliary/Build/vcvars64.bat"
    if not vs_path or not vcvars.is_file():
        die("vcvars64.bat not found; install the VC++ x64 toolset.")

    print(f"==> Loading MSVC environment ({vs_path})")
    # Pass as one string, not a list: list2cmdline would mangle the quotes around
    # the space-containing vcvars path. `>nul` drops the banner, leaving `set` output.
    out = subprocess.run(
        f'cmd /c "{vcvars}" >nul && set',
        check=True, text=True, capture_output=True,
    ).stdout
    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep and key and " " not in key:
            os.environ[key] = value

    if not shutil.which("cl"):
        die("cl still not on PATH after vcvars.")


def build(repo_root: Path, preset: str, *, run_tests: bool = True) -> None:
    """Conan install + CMake build for one preset under repo_root.

    run_tests=False replaces the workflow preset (configure+build+ctest) with
    configure+build, so CI can time and report tests as a separate step.
    """
    require_build_tools()
    build_type = "Debug" if "debug" in preset else "Release"

    # Own profiles first (cs2-kit standalone); vendored cs2-kit's otherwise, so
    # consuming repos don't carry duplicate copies that drift.
    profiles = repo_root / "conan/profiles"
    if not profiles.is_dir():
        profiles = repo_root / "vendor/cs2-kit/conan/profiles"
    if not profiles.is_dir():
        die(
            "no Conan profiles at conan/profiles or "
            f"vendor/cs2-kit/conan/profiles under {repo_root}"
        )
    settings = ["-s", f"build_type={build_type}"]
    if preset.startswith("linux-"):
        profile = profiles / "linux-steamrt.txt"
    elif preset.startswith("windows-"):
        profile = profiles / "windows-msvc.txt"
        settings += ["-s", f"compiler.runtime_type={build_type}"]
        ensure_msvc_env()
    else:
        die(f"Unknown preset: {preset}")

    # Lockfile only when the repo pins one; fresh projects build unpinned until
    # they run `conan lock create`.
    lock = repo_root / "conan.lock"
    lock_args = ["--lockfile", str(lock)] if lock.is_file() else []

    build_dir = repo_root / "build" / preset
    run_tool(
        "conan", "install", str(repo_root),
        "--output-folder", str(build_dir / "generators"),
        "--build=missing",
        *lock_args,
        "--profile:host", str(profile),
        "--profile:build", str(profile),
        *settings,
    )

    if run_tests:
        run_tool("cmake", "--workflow", "--preset", preset)
    else:
        run_tool("cmake", "--preset", preset)
        run_tool("cmake", "--build", "--preset", preset)

    print(f"\n=== Build Complete ===\nPreset: {preset}\nBuild directory: build/{preset}")
