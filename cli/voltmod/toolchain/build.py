"""Building, testing and formatting one preset of a VoltMod project."""

import os
import shutil
from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.panorama.render import render_screens
from voltmod.project import Project
from voltmod.toolchain.checkout import (
    build_checkout,
    check_build_uses_package,
    relock_framework,
)
from voltmod.toolchain.conan import (
    SDK_BUILD_EXCLUSIONS,
    editable_framework,
    ensure_remote,
    profile_args,
)
from voltmod.toolchain.msvc import load_msvc_environment
from voltmod.toolchain.process import run, run_tool

FRAMEWORK_REPOSITORY = "https://github.com/voltygg/voltmod.git"

# Lets ccache reuse objects built with force-included precompiled headers.
CCACHE_SETTINGS = {
    "CCACHE_SLOPPINESS": "pch_defines,time_macros,locale,include_file_ctime,include_file_mtime",
    "CCACHE_DEPEND": "1",
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

    `relock` turns the editable framework into the package CI resolves, and verifies afterwards
    that this build was configured against it.
    """
    uses_ccache = _configure_ccache(project.root)
    ensure_remote(project.root)
    host_profile = profile_args(project.root, preset)
    load_msvc_environment()

    package_folder = relock_framework(project, preset) if relock else ""

    # An editable framework is linked in place, so it compiles first.
    if checkout := editable_framework(project.root):
        build_checkout(project, checkout, preset)

    # Right after an SDK bump the lockfile does not pin the new revisions, so CI builds without it.
    lock_args: list[str | Path] = []
    if use_lockfile and project.lockfile.is_file():
        lock_args = ["--lockfile", project.lockfile]
    run_tool(
        "conan",
        "install",
        project.root,
        "--output-folder",
        project.root,
        "--build=missing",
        *SDK_BUILD_EXCLUSIONS,
        *lock_args,
        *(conan_options or []),
        *host_profile,
    )

    # Writes only what changed, so unchanged screens trigger no rebuild.
    render_screens(project.root)

    if uses_ccache:
        run("ccache", "-z", check=False)
    run_tool("cmake", "--preset", preset)
    run_tool("cmake", "--build", "--preset", preset)
    if uses_ccache:
        run("ccache", "-s", "-v", check=False)
    if relock:
        check_build_uses_package(project, preset, package_folder)

    print(f"\nBuild complete: {preset} -> build/{preset}")


def run_tests(project: Project, preset: str, name_filter: str = "") -> None:
    if not project.build_dir(preset).is_dir():
        raise VoltmodError(f"no build at build/{preset}; run `voltmod build -p {preset}` first")
    load_msvc_environment()
    # Rebuild first so stale test binaries never run.
    run_tool("cmake", "--build", "--preset", preset)
    run_tool("ctest", "--preset", preset, *(["-R", name_filter] if name_filter else []))


def bootstrap(project: Project, preset: str) -> None:
    print("==> [1/2] Installing Conan profiles and remotes")
    local_config = project.root / "conan"
    if (local_config / "profiles").is_dir():
        source = [str(local_config)]
    else:
        source = [FRAMEWORK_REPOSITORY, "-sf", "conan"]
    run_tool("conan", "config", "install", *source)

    print("==> [2/2] Building with Conan + CMake")
    build(project, preset)
    print("\nBootstrap complete: build/<preset>/plugins/")


def _configure_ccache(root: Path) -> bool:
    if not shutil.which("ccache"):
        return False
    for key, value in CCACHE_SETTINGS.items():
        os.environ.setdefault(key, value)
    os.environ.setdefault("CCACHE_BASEDIR", str(root))
    return True
