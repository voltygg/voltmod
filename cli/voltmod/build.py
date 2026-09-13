"""Building, testing and formatting one preset of a VoltMod project."""

import os
import shutil
import subprocess
from pathlib import Path

from voltmod.conan import SDK_BUILD_EXCLUSIONS, ensure_remote, find_editable_framework, profile_args
from voltmod.errors import VoltmodError
from voltmod.framework_checkout import build_checkout, check_build_uses_package, relock_framework
from voltmod.panorama.render import render_screens
from voltmod.process import find_tool, load_msvc_environment, require_build_tools, run_tool
from voltmod.project import Project

FRAMEWORK_REPOSITORY = "https://github.com/voltygg/voltmod.git"
CPP_SUFFIXES = (".cpp", ".hpp", ".inc")

# Stay well under Windows' 32767-character command-line limit.
MAX_COMMAND_LINE = 24000

# Lets ccache reuse objects built with force-included precompiled headers.
CCACHE_SETTINGS = {
    "CCACHE_SLOPPINESS": "pch_defines,time_macros,locale,include_file_ctime,include_file_mtime",
    "CCACHE_DEPEND": "1",
    "CCACHE_MAXSIZE": "1G",
}


def build(
    project: Project,
    preset: str,
    *,
    conan_options: list[str] | None = None,
    use_lockfile: bool = True,
    relock: bool = False,
) -> None:
    """Conan install, screen rendering, and the CMake build for one preset.

    @p relock turns the editable framework into the package CI resolves, and verifies afterwards
    that this build was configured against it.
    """
    require_build_tools()
    uses_ccache = _configure_ccache(project.root)
    ensure_remote(project.root)
    host_profile = profile_args(project.root, preset)
    load_msvc_environment()

    package_folder = relock_framework(project, preset) if relock else ""

    # An editable framework is linked in place, so it compiles first.
    checkout = find_editable_framework()
    if checkout and checkout.resolve() != project.root.resolve():
        build_checkout(project, checkout, preset)

    # Right after an SDK bump the lockfile does not pin the new revisions, so CI builds without it.
    lock_args = []
    if use_lockfile and project.lockfile.is_file():
        lock_args = ["--lockfile", str(project.lockfile)]
    run_tool(
        "conan", "install", str(project.root), "--output-folder", str(project.root),
        "--build=missing", *SDK_BUILD_EXCLUSIONS, *lock_args, *(conan_options or []),
        *host_profile,
    )

    # Writes only what changed, so unchanged screens trigger no rebuild.
    render_screens(project.root, [])

    if uses_ccache:
        subprocess.run(["ccache", "-z"], check=False)
    run_tool("cmake", "--preset", preset)
    run_tool("cmake", "--build", "--preset", preset)
    if uses_ccache:
        subprocess.run(["ccache", "-s", "-v"], check=False)
    if relock:
        check_build_uses_package(project, preset, package_folder)

    print(f"\nBuild complete: {preset} -> build/{preset}")


def run_tests(project: Project, preset: str, name_filter: str = "") -> None:
    if not project.build_dir(preset).is_dir():
        raise VoltmodError(f"no build at build/{preset}; run `voltmod build {preset}` first")
    load_msvc_environment()
    # Rebuild first so stale test binaries never run.
    run_tool("cmake", "--build", "--preset", preset)
    run_tool("ctest", "--preset", preset, *(["-R", name_filter] if name_filter else []))


def bootstrap(project: Project) -> None:
    print("==> [1/2] Installing Conan profiles and remotes")
    local_config = project.root / "conan"
    if (local_config / "profiles").is_dir():
        source = [str(local_config)]
    else:
        source = [FRAMEWORK_REPOSITORY, "-sf", "conan"]
    run_tool("conan", "config", "install", *source)

    print("==> [2/2] Building with Conan + CMake")
    build(project, project.resolve_preset())
    print("\nBootstrap complete: build/<preset>/plugins/")


def find_cpp_sources(root: Path, dirs: list[str]) -> list[Path]:
    return sorted(
        path
        for name in dirs
        if (root / name).is_dir()
        for path in (root / name).rglob("*")
        if path.suffix in CPP_SUFFIXES
    )


def format_cpp_files(files: list[Path]) -> None:
    """Run clang-format in place, in batches that fit on a Windows command line."""
    batch: list[str] = []
    length = 0
    for file in map(str, files):
        if batch and length + len(file) + 3 > MAX_COMMAND_LINE:
            run_tool("clang-format", "-i", *batch)
            batch, length = [], 0
        batch.append(file)
        length += len(file) + 3  # quotes and a separator
    if batch:
        run_tool("clang-format", "-i", *batch)


def format_cpp_text(text: str, path: Path) -> str:
    """@p text as clang-format writes it at @p path, using the nearest .clang-format."""
    # Bytes, so Windows newline translation cannot change what clang-format reads.
    result = subprocess.run(
        [find_tool("clang-format"), f"--assume-filename={path}"],
        input=text.encode("utf-8"),
        capture_output=True,
        check=True,
    )
    return result.stdout.decode("utf-8")


def _configure_ccache(root: Path) -> bool:
    if not shutil.which("ccache"):
        return False
    for key, value in CCACHE_SETTINGS.items():
        os.environ.setdefault(key, value)
    os.environ.setdefault("CCACHE_BASEDIR", str(root))
    return True
