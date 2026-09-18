"""The Conan packages this repository publishes, and how each is built and uploaded."""

import os
from pathlib import Path
from typing import Any

from voltmod.conan import REMOTE, SDK_BUILD_EXCLUSIONS, ensure_remote, profile_args, run_conan_json
from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, msvc_version, run_tool
from voltmod.project import default_preset

# Dependency order: the SDKs, then the framework that consumes them.
SDK_PACKAGES = ("metamod-source", "hl2sdk-cs2", "sqlpp23")
FRAMEWORK_PACKAGE = "voltmod"

# Their package id is platform-neutral, so only one runner may publish a revision.
HEADER_ONLY_PACKAGES = frozenset({"metamod-source", "sqlpp23"})


def create_package(root: Path, recipe: Path, *args: str) -> None:
    """Create one package with the release profile, the way a build resolves it."""
    settings = profile_args(root, default_preset())
    if WINDOWS:
        # The runner's own cl, rather than the newest one the profile names.
        settings += ["-s", f"compiler.version={msvc_version()}"]
    run_tool("conan", "create", str(recipe), *settings, *args)


def upload_packages(pattern: str) -> None:
    retry = ("-cc", "core.upload:retry=3", "-cc", "core.upload:retry_wait=10")
    run_tool("conan", "upload", pattern, "-r", REMOTE, "--confirm", *retry)


def log_in(root: Path) -> None:
    user = os.environ.get("CLOUDSMITH_USERNAME")
    key = os.environ.get("CLOUDSMITH_API_KEY")
    if not user or not key:
        raise VoltmodError("CLOUDSMITH_USERNAME and CLOUDSMITH_API_KEY are required to publish")
    ensure_remote(root)
    run_tool("conan", "remote", "login", REMOTE, user, "-p", key)


def is_published(name: str, version: str) -> bool:
    # A missing recipe still exits 0, listed as an "error" entry instead of a reference.
    listing = run_conan_json("list", f"{name}/{version}", "-r", REMOTE).get(REMOTE, {})
    return any(reference.startswith(f"{name}/") for reference in listing)


def build_sdks(root: Path) -> None:
    # sqlpp23's client libraries come from conancenter and rarely have prebuilt binaries.
    for name in SDK_PACKAGES:
        create_package(root, root / "recipes" / name, "--build=missing")


def build_framework(root: Path, *, use_lockfile: bool) -> None:
    args = ["--build=missing", *SDK_BUILD_EXCLUSIONS]
    if not use_lockfile:
        args.append("--lockfile=")
    create_package(root, root, *args)
    check_package_contents(framework_version(root))


def required_files(package_settings: dict[str, Any]) -> tuple[str, ...]:
    """What a framework package must hold, for the platform it was built for."""
    windows = package_settings.get("os") == "Windows"
    host_bin = "addons/voltmod/bin/win64" if windows else "addons/voltmod/bin/linuxsteamrt64"
    libraries = (
        ("lib/voltmod-sdk.lib", "lib/voltmod-database.lib")
        if windows
        else ("lib/libvoltmod-sdk.a", "lib/libvoltmod-database.a")
    )
    return (
        *libraries,
        # The host module, its Metamod entry and its gamedata. Nothing links these, so a
        # packaging mistake would otherwise only surface when a server fails to start.
        f"{host_bin}/voltmod.{'dll' if windows else 'so'}",
        "addons/metamod/voltmod.vdf",
        "addons/voltmod/gamedata/gamedata.jsonc",
        "include/VoltMod/Api.hpp",
        "cmake/VoltModPlugin.cmake",
    )


def check_package_contents(version: str) -> None:
    """Refuse to publish a package that a server could not be installed from."""
    listing = run_conan_json(
        "list", f"{FRAMEWORK_PACKAGE}/{version}#latest:*", "-c"
    ).get("Local Cache", {})
    checked = 0
    for reference, body in listing.items():
        # A missing recipe still exits 0, listed as an "error" entry instead of a reference.
        if not reference.startswith(f"{FRAMEWORK_PACKAGE}/"):
            continue
        for revision, contents in body.get("revisions", {}).items():
            for package_id, package in contents.get("packages", {}).items():
                folder = package_folder(f"{reference}#{revision}:{package_id}")
                settings = package.get("info", {}).get("settings", {})
                missing = [name for name in required_files(settings)
                           if not (folder / name).exists()]
                if missing:
                    raise VoltmodError(
                        f"{reference}:{package_id} is missing {', '.join(missing)}; "
                        "check the install() rules in CMakeLists.txt"
                    )
                checked += 1
    if not checked:
        raise VoltmodError(f"no {FRAMEWORK_PACKAGE}/{version} package in the local cache to check")


def package_folder(reference: str) -> Path:
    return Path(run_conan_json("cache", "path", reference)["cache_path"])


def framework_version(root: Path) -> str:
    """The package version, read through Conan, which owns that metadata."""
    version = run_conan_json("inspect", str(root)).get("version")
    if not version:
        raise VoltmodError("the voltmod Conan recipe has no version")
    return version


def check_release_tag(root: Path) -> None:
    """A v* tag must match the Conan package version."""
    tag = os.environ.get("GITHUB_REF_NAME", "")
    if not tag.startswith("v"):
        return
    declared = framework_version(root)
    if tag[1:] != declared:
        raise VoltmodError(f"tag {tag} does not match conanfile.py ({declared})")
