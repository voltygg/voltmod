"""Shared toolchain helpers for VoltMod and its consumers."""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn

WINDOWS = sys.platform == "win32"
CONAN_REMOTE = "volty"
# Linux CI consumes published SDK binaries; Windows builds them locally when missing.
SDK_BUILD_EXCLUSIONS = ()
if not WINDOWS:
    SDK_BUILD_EXCLUSIONS = ("--build=!hl2sdk-cs2/*", "--build=!metamod-source/*")
CPP_EXTS = (".cpp", ".hpp")
# Under the 32767-character Windows cap, with room for the tool path and flags.
MAX_COMMAND_LINE = 24000


def templates_dir() -> Path:
    """Return scaffold templates from the installed wheel or repository."""
    packaged = Path(__file__).resolve().parent / "templates"
    return packaged if packaged.is_dir() else Path(__file__).resolve().parents[2] / "templates"


def remote_url() -> str:
    """Read the package remote from conan/remotes.json."""
    for base in (Path.cwd(), Path(__file__).resolve().parents[2]):
        remotes = base / "conan/remotes.json"
        if remotes.is_file():
            for entry in json.loads(remotes.read_text(encoding="utf-8"))["remotes"]:
                if entry["name"] == CONAN_REMOTE:
                    return entry["url"]
    die(f"no '{CONAN_REMOTE}' entry in any conan/remotes.json")


def die(message: str) -> NoReturn:
    """Exit with an error message."""
    raise SystemExit(f"ERROR: {message}")


def default_preset() -> str:
    """Pick the release preset for the current OS."""
    return "windows-msvc-release" if WINDOWS else "linux-steamrt-release"


def resolve_tool(tool: str) -> tuple[list[str], dict[str, str]]:
    """Resolve a build tool from the project venv, uv, or PATH."""
    for root in filter(None, (os.environ.get("VIRTUAL_ENV"), ".venv")):
        bindir = Path(root) / ("Scripts" if WINDOWS else "bin")
        exe = shutil.which(tool, path=str(bindir))
        if exe:
            return [exe], {**os.environ, "PATH": f"{bindir}{os.pathsep}{os.environ['PATH']}"}

    if shutil.which("uv"):
        return ["uv", "run", tool], dict(os.environ)
    if shutil.which(tool):
        return [tool], dict(os.environ)
    die(
        f"'{tool}' was not found on PATH. Install CMake 4.3.4+, Conan 2.29.1+, and "
        "Ninja, or install uv and run `uv sync`."
    )


def run_tool(
    tool: str,
    *args: str,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    """Run a resolved build tool."""
    argv, env = resolve_tool(tool)
    return subprocess.run([*argv, *args], check=check, env=env, text=True, capture_output=capture)


def ensure_remote() -> None:
    """Register Cloudsmith unless VOLTMOD_SKIP_REMOTE_SETUP is set."""
    if os.environ.get("VOLTMOD_SKIP_REMOTE_SETUP"):
        return
    listing = run_tool("conan", "remote", "list", capture=True, check=False)
    if listing.returncode == 0 and f"{CONAN_REMOTE}:" in listing.stdout:
        return
    url = remote_url()
    print(f"==> Adding Conan remote '{CONAN_REMOTE}' ({url})")
    run_tool("conan", "remote", "add", "--force", CONAN_REMOTE, url)


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
    """Format or check C++ files below the selected directories."""
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

    # Stay below Windows' command-line limit on every platform.
    failure = 0
    for batch in _chunk_by_length(files, MAX_COMMAND_LINE):
        try:
            run_tool("clang-format", *args, *batch)
        except subprocess.CalledProcessError as e:
            # Keep going so --check reports every offending file, not just the first batch.
            failure = failure or e.returncode
    if failure:
        raise SystemExit(failure)
    print(f"clang-format {'checked' if check else 'formatted'} {len(files)} file(s).")


def _tool_version(tool: str, prefix: str) -> tuple[int, ...]:
    """Read `<tool> --version` and parse the dotted number after `prefix`."""
    out = run_tool(tool, "--version", capture=True).stdout
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


def _vswhere() -> Path:
    path = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not path.is_file():
        die("vswhere not found; install Visual Studio with C++ tools.")
    return path


def msvc_version() -> str:
    """Return the newest installed cl version in Conan form, such as ``195``."""
    found = subprocess.run(
        [
            str(_vswhere()),
            "-latest",
            "-products",
            "*",
            "-find",
            r"VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe",
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.splitlines()
    if not found:
        die("no cl.exe found")
    # cl with no arguments prints its banner on stderr, then a usage summary.
    banner = subprocess.run([sorted(found)[-1]], text=True, capture_output=True).stderr
    match = re.search(r"Version (\d+)\.(\d+)", banner)
    if not match:
        die("could not parse the cl version banner")
    version = f"{match.group(1)}{match.group(2)[0]}"
    if int(version) < 193:
        die(f"cl {version} predates C++23 support")
    return version


def ensure_msvc_env() -> None:
    """Import the MSVC toolchain (cl + INCLUDE/LIB) into os.environ on Windows."""
    if not WINDOWS or shutil.which("cl"):
        return

    vs_path = subprocess.run(
        [
            str(_vswhere()),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    vcvars = Path(vs_path) / "VC/Auxiliary/Build/vcvars64.bat"
    if not vs_path or not vcvars.is_file():
        die("vcvars64.bat not found; install the VC++ x64 toolset.")

    print(f"==> Loading MSVC environment ({vs_path})")
    # Pass as one string, not a list: list2cmdline would mangle the quotes around
    # the space-containing vcvars path. `>nul` drops the banner, leaving `set` output.
    out = subprocess.run(
        f'cmd /c "{vcvars}" >nul && set',
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep and key and " " not in key:
            os.environ[key] = value

    if not shutil.which("cl"):
        die("cl still not on PATH after vcvars.")


# Settings required to cache force-included precompiled headers.
CCACHE_SETTINGS = {
    "CCACHE_SLOPPINESS": "pch_defines,time_macros,locale,include_file_ctime,include_file_mtime",
    "CCACHE_DEPEND": "1",
    "CCACHE_MAXSIZE": "1G",
}


def _prepare_ccache(repo_root: Path) -> bool:
    """Apply the PCH-compatible ccache settings. False if ccache is not in play."""
    if not shutil.which("ccache"):
        return False
    for key, value in CCACHE_SETTINGS.items():
        os.environ.setdefault(key, value)
    os.environ.setdefault("CCACHE_BASEDIR", str(repo_root))
    return True


def _ccache(*args: str) -> None:
    subprocess.run(["ccache", *args], check=False)


def build(
    repo_root: Path, preset: str, *, run_tests: bool = True, options: list[str] | None = None
) -> None:
    """Run Conan install and the selected CMake preset."""
    require_build_tools()
    ccache = _prepare_ccache(repo_root)
    ensure_remote()
    build_type = "Debug" if "debug" in preset else "Release"

    conan_home = Path(os.environ.get("CONAN_HOME", Path.home() / ".conan2"))
    profile_dirs = (
        repo_root / "conan/profiles",
        repo_root / "vendor/voltmod/conan/profiles",
        conan_home / "profiles",
    )
    profiles = next((path for path in profile_dirs if path.is_dir()), None)
    if profiles is None:
        die("no Conan profiles found; run `voltmod bootstrap` or the setup-toolchain action")
    settings = ["-s", f"build_type={build_type}"]
    if preset.startswith("linux-"):
        profile = profiles / "linux-steamrt.txt"
    elif preset.startswith("windows-"):
        profile = profiles / "windows-msvc.txt"
        settings += ["-s", f"compiler.runtime_type={build_type}"]
        ensure_msvc_env()
    else:
        die(f"Unknown preset: {preset}")

    lock = repo_root / "conan.lock"
    lock_args = ["--lockfile", str(lock)] if lock.is_file() else []

    run_tool(
        "conan",
        "install",
        str(repo_root),
        "--output-folder",
        str(repo_root),
        "--build=missing",
        *SDK_BUILD_EXCLUSIONS,
        *lock_args,
        *(options or []),
        "--profile:host",
        str(profile),
        "--profile:build",
        str(profile),
        *settings,
    )

    if ccache:
        _ccache("-z")

    if run_tests:
        run_tool("cmake", "--workflow", "--preset", preset)
    else:
        run_tool("cmake", "--preset", preset)
        run_tool("cmake", "--build", "--preset", preset)

    if ccache:
        _ccache("-s", "-v")

    print(f"\nBuild complete: {preset} -> build/{preset}")
