"""Building, testing and formatting one preset of a VoltMod project."""

import os
import shutil
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.panorama.render import render_screens
from voltmod.project import Project
from voltmod.toolchain.checkout import (
    build_checkout,
    check_build_uses_package,
    relock_framework,
)
from voltmod.toolchain.conan import (
    PREBUILT_SDK_ARGS,
    ensure_remote,
    linked_checkout,
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

    `relock` pins the editable framework as the package CI resolves, then checks the build used it.
    """
    uses_ccache = _configure_ccache(project.root)
    ensure_remote(project.root)
    host_profile = profile_args(project.root, preset)
    load_msvc_environment()

    package_folder = relock_framework(project, preset) if relock else ""

    # An editable framework is linked in place, so it compiles first.
    if checkout := linked_checkout(project.root):
        build_checkout(project, checkout, preset)

    _conan_install(project, host_profile, conan_options or [], use_lockfile)
    # Writes only what changed, so unchanged screens trigger no rebuild.
    render_screens(project.root)
    with _ccache_stats(uses_ccache):
        run_tool("cmake", "--preset", preset)
        run_tool("cmake", "--build", "--preset", preset)

    if relock:
        check_build_uses_package(project, preset, package_folder)

    console.done(f"Build complete: {preset} -> build/{preset}")


def run_tests(project: Project, preset: str, name_filter: str = "") -> None:
    if not project.build_dir(preset).is_dir():
        raise VoltmodError(f"no build at build/{preset}; run `voltmod build -p {preset}` first")
    load_msvc_environment()
    # Rebuild first so stale test binaries never run.
    run_tool("cmake", "--build", "--preset", preset)
    run_tool("ctest", "--preset", preset, *(["-R", name_filter] if name_filter else []))


def bootstrap(project: Project, preset: str) -> None:
    console.step("[1/2] Installing Conan profiles and remotes")
    local_config = project.root / "conan"
    if (local_config / "profiles").is_dir():
        source = [str(local_config)]
    else:
        source = [FRAMEWORK_REPOSITORY, "-sf", "conan"]
    run_tool("conan", "config", "install", *source)

    console.step("[2/2] Building with Conan + CMake")
    build(project, preset)
    console.done(f"Bootstrap complete: build/{preset}/plugins/")


def _conan_install(
    project: Project, host_profile: list[str], options: list[str], use_lockfile: bool
) -> None:
    lock: list[str | Path] = []
    # Right after an SDK bump the lockfile does not pin the new revisions, so CI builds without it.
    if use_lockfile and project.lockfile.is_file():
        lock = ["--lockfile", project.lockfile]
    output: list[str | Path] = ["--output-folder", project.root]
    build_policy = ["--build=missing", *PREBUILT_SDK_ARGS]
    run_tool(
        "conan", "install", project.root, *output, *build_policy, *lock, *options, *host_profile
    )


@contextmanager
def _ccache_stats(enabled: bool) -> Iterator[None]:
    """Zero ccache's counters before the block and report them after it."""
    if enabled:
        run("ccache", "-z", check=False)
    yield
    if enabled:
        run("ccache", "-s", "-v", check=False)


def _configure_ccache(root: Path) -> bool:
    if not shutil.which("ccache"):
        return False
    for key, value in CCACHE_SETTINGS.items():
        os.environ.setdefault(key, value)
    os.environ.setdefault("CCACHE_BASEDIR", str(root))
    return True
