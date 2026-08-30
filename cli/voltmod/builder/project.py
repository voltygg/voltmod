"""Building, testing and formatting one preset of a VoltMod project."""

import os
import shutil
import subprocess
from pathlib import Path

from ..tools import (
    SDK_BUILD_EXCLUSIONS,
    die,
    ensure_msvc_env,
    ensure_remote,
    host_profile,
    require_build_tools,
    run_tool,
)
from . import framework

CPP_EXTS = (".cpp", ".hpp")
# Leave room below Windows' 32767-character command-line limit.
MAX_COMMAND_LINE = 24000

# Make force-included precompiled headers cacheable.
CCACHE_SETTINGS = {
    "CCACHE_SLOPPINESS": "pch_defines,time_macros,locale,include_file_ctime,include_file_mtime",
    "CCACHE_DEPEND": "1",
    "CCACHE_MAXSIZE": "1G",
}


def build(
    repo_root: Path,
    preset: str,
    *,
    run_tests: bool = True,
    options: list[str] | None = None,
    use_lockfile: bool = True,
    relock: bool = False,
) -> None:
    """Run Conan install and the selected CMake preset.

    `relock` turns the editable checkout into the cache package CI resolves before building, and
    verifies afterwards that the build tree was reconfigured against it. Both halves live here
    because the verification is only meaningful either side of this build.
    """
    require_build_tools()
    ccache = _prepare_ccache(repo_root)
    ensure_remote()
    profile, settings = host_profile(repo_root, preset)
    ensure_msvc_env()

    package_folder = framework.relock(repo_root, preset) if relock else ""

    # An editable checkout is linked in place, so it has to be compiled first - the repository
    # being built is skipped, as `voltmod build` inside the checkout already compiles it.
    checkout = framework.editable()
    if checkout and checkout.resolve() != repo_root.resolve():
        framework.build(repo_root, checkout, preset)

    # CI builds against SDK packages it just created from the HEAD recipes, whose revisions the
    # committed lockfile does not pin yet; `package build kit --no-lockfile` skips it the same way.
    lock = repo_root / "conan.lock"
    lock_args = ["--lockfile", str(lock)] if use_lockfile and lock.is_file() else []
    run_tool(
        "conan", "install", str(repo_root), "--output-folder", str(repo_root),
        "--build=missing", *SDK_BUILD_EXCLUSIONS, *lock_args, *(options or []),
        "--profile:host", str(profile), "--profile:build", str(profile), *settings,
    )

    if ccache:
        subprocess.run(["ccache", "-z"], check=False)
    run_tool("cmake", "--preset", preset)
    run_tool("cmake", "--build", "--preset", preset)
    if run_tests:
        run_tool("ctest", "--preset", preset)
    if ccache:
        subprocess.run(["ccache", "-s", "-v"], check=False)
    if relock:
        framework.verify(repo_root, preset, package_folder)

    print(f"\nBuild complete: {preset} -> build/{preset}")


def test(repo_root: Path, preset: str, *, filter_: str = "") -> None:
    """Bring the build up to date, then run its CTest preset."""
    if not (repo_root / "build" / preset).is_dir():
        die(f"no build at build/{preset}; run `voltmod build {preset}` first")
    # Never run stale test binaries.
    ensure_msvc_env()
    run_tool("cmake", "--build", "--preset", preset)
    run_tool("ctest", "--preset", preset, *(["-R", filter_] if filter_ else []))


def format_sources(repo_root: Path, dirs: list[str]) -> None:
    """Rewrite C++ files below the selected directories in the pinned style."""
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
    for batch in _chunk_by_length(files, MAX_COMMAND_LINE):
        run_tool("clang-format", "-i", *batch)
    print(f"clang-format formatted {len(files)} file(s).")


def _prepare_ccache(repo_root: Path) -> bool:
    """Apply the PCH-compatible ccache settings. False if ccache is not in play."""
    if not shutil.which("ccache"):
        return False
    for key, value in CCACHE_SETTINGS.items():
        os.environ.setdefault(key, value)
    os.environ.setdefault("CCACHE_BASEDIR", str(repo_root))
    return True


def _chunk_by_length(files: list[str], budget: int) -> list[list[str]]:
    """Split `files` into batches whose joined length stays under `budget` characters."""
    batches: list[list[str]] = [[]]
    used = 0
    for f in files:
        cost = len(f) + 3  # quotes and separator
        if batches[-1] and used + cost > budget:
            batches.append([])
            used = 0
        batches[-1].append(f)
        used += cost
    return [b for b in batches if b]
