"""Shared build helpers for voltmod and the projects that depend on it.

Import, don't execute. This is the single source of truth for the toolchain
invocation; consuming repos get it from the installed voltmod distribution.
"""

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
# Never build an SDK locally: a missing binary means its publish job never ran, and
# --build=missing would start a multi-GB upstream fetch that fails anyway.
SDK_BUILD_EXCLUSIONS = ("--build=!hl2sdk-cs2/*", "--build=!metamod-source/*")
CPP_EXTS = (".cpp", ".hpp")
# Under the 32767-character Windows cap, with room for the tool path and flags.
MAX_COMMAND_LINE = 24000


def templates_dir() -> Path:
    """The scaffold tree: packaged beside this module in a wheel, at the repo root
    in a checkout."""
    packaged = Path(__file__).resolve().parent / "templates"
    return packaged if packaged.is_dir() else Path(__file__).resolve().parents[2] / "templates"


def remote_url() -> str:
    """The package remote, read from conan/remotes.json - the file `conan config install`
    ships, so CI and a developer's machine cannot disagree about where packages live."""
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
    """Locate a build tool, preferring the project venv, then `uv run`, then PATH.

    Returns the argv prefix to invoke it with and the environment it needs.
    """
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


def run_tool(tool: str, *args: str) -> subprocess.CompletedProcess[bytes]:
    """Run a build tool, failing the process if it exits non-zero."""
    argv, env = resolve_tool(tool)
    return subprocess.run([*argv, *args], check=True, env=env)


def ensure_remote() -> None:
    """Register the public Cloudsmith remote that serves hl2sdk-cs2 and metamod-source.

    Writes to the global Conan config, so VOLTMOD_SKIP_REMOTE_SETUP=1 opts out for
    anyone who manages remotes themselves (a proxy, a mirror, an air-gapped cache).
    """
    if os.environ.get("VOLTMOD_SKIP_REMOTE_SETUP"):
        return
    argv, env = resolve_tool("conan")
    listing = subprocess.run(
        [*argv, "remote", "list"], text=True, capture_output=True, env=env
    )
    if listing.returncode == 0 and f"{CONAN_REMOTE}:" in listing.stdout:
        return
    url = remote_url()
    print(f"==> Adding Conan remote '{CONAN_REMOTE}' ({url})")
    subprocess.run(
        [*argv, "remote", "add", "--force", CONAN_REMOTE, url],
        check=True, env=env,
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

    rglob only descends the listed dirs, so nothing outside them is touched.
    --check leaves files as they are and exits non-zero on any diff.
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

    # A single argv with every file exceeds the 32767-character Windows command-line cap
    # (WinError 206, nothing formatted). Batch on POSIX too, so both take the same path.
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


def _vswhere() -> Path:
    path = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not path.is_file():
        die("vswhere not found; install Visual Studio with C++ tools.")
    return path


def msvc_version() -> str:
    """The runner's cl version as Conan spells it, e.g. "195".

    Hosted Windows runners ship a newer cl than the checked-in profile names, and a
    package built under the wrong compiler.version gets a package id no consumer
    resolves. -find lists every installed toolset ascending, so take the last: a VS2022
    image also carries the 14.29 (cl 19.2x) toolset, which cannot compile C++23.
    """
    found = subprocess.run(
        [str(_vswhere()), "-latest", "-products", "*",
         "-find", r"VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe"],
        check=True, text=True, capture_output=True,
    ).stdout.splitlines()  # not .split(): the paths contain "Program Files"
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
        [str(_vswhere()), "-latest", "-products", "*", "-requires",
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


# What ccache needs to be useful against a force-included precompiled header. Every CI
# job that builds this project restated these; they are a property of the kit's PCH, not
# of any one runner, so the kit asserts them. An explicitly set value always wins.
CCACHE_SETTINGS = {
    # Without pch_defines AND depend mode, every PCH-using TU is a permanent miss.
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
    # Rewrites absolute paths in the hash: a container sees a different workspace path
    # than the host that saved the cache, and without this every entry misses.
    os.environ.setdefault("CCACHE_BASEDIR", str(repo_root))
    return True


def _ccache(*args: str) -> None:
    subprocess.run(["ccache", *args], check=False)


def build(repo_root: Path, preset: str, *, run_tests: bool = True,
          options: list[str] | None = None) -> None:
    """Conan install + CMake build for one preset under repo_root.

    run_tests=False replaces the workflow preset (configure+build+ctest) with
    configure+build, so CI can time and report tests as a separate step. `options` are
    extra `conan install` arguments, typically -o pairs.
    """
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

    # Lockfile only when the repo pins one; fresh projects build unpinned until
    # they run `conan lock create`.
    lock = repo_root / "conan.lock"
    lock_args = ["--lockfile", str(lock)] if lock.is_file() else []

    # The recipe's layout() decides where the toolchain and generators land; passing the
    # repo root keeps that the single description instead of restating it here.
    run_tool(
        "conan", "install", str(repo_root),
        "--output-folder", str(repo_root),
        "--build=missing",
        *SDK_BUILD_EXCLUSIONS,
        *lock_args,
        *(options or []),
        "--profile:host", str(profile),
        "--profile:build", str(profile),
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
        # A warm rerun of an unchanged tree should show >95% hits; a collapse means
        # the settings above stopped matching how the compiler is invoked.
        _ccache("-s", "-v")

    print(f"\n=== Build Complete ===\nPreset: {preset}\nBuild directory: build/{preset}")
