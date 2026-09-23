"""The Conan packages this repository publishes, and how each is built and uploaded."""

import os
from collections.abc import Iterator
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError
from voltmod.platforms import Platform
from voltmod.project import default_preset
from voltmod.server.install import HOST_GAMEDATA, HOST_VDF, host_binary
from voltmod.toolchain.conan import (
    REMOTE,
    SDK_BUILD_EXCLUSIONS,
    ensure_remote,
    profile_args,
    run_conan_json,
)
from voltmod.toolchain.msvc import msvc_version
from voltmod.toolchain.process import WINDOWS, run_tool

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
    run_tool("conan", "create", recipe, *settings, *args)


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


def references(listing: dict[str, Any], name: str) -> dict[str, Any]:
    """The listing's entries for `name`; `conan list` reports a miss as an "error" entry."""
    return {
        reference: body for reference, body in listing.items() if reference.startswith(f"{name}/")
    }


def is_published(name: str, version: str) -> bool:
    listing = run_conan_json("list", f"{name}/{version}", "-r", REMOTE).get(REMOTE, {})
    return bool(references(listing, name))


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
    platform = Platform.WINDOWS if windows else Platform.LINUX
    libraries = (
        ("lib/voltmod-portable.lib", "lib/voltmod-sdk.lib", "lib/voltmod-database.lib")
        if windows
        else ("lib/libvoltmod-portable.a", "lib/libvoltmod-sdk.a", "lib/libvoltmod-database.a")
    )
    return (
        *libraries,
        # Nothing links the host, so a packaging mistake in it only shows up on a live server.
        host_binary(platform),
        HOST_VDF,
        HOST_GAMEDATA,
        "include/VoltMod/Api.hpp",
        "cmake/VoltModPlugin.cmake",
    )


def cached_packages(version: str) -> Iterator[tuple[str, dict[str, Any]]]:
    """Every full package reference the local cache holds for `version`, with its metadata."""
    listing = run_conan_json("list", f"{FRAMEWORK_PACKAGE}/{version}#latest:*", "-c").get(
        "Local Cache", {}
    )
    for reference, body in references(listing, FRAMEWORK_PACKAGE).items():
        for revision, contents in body.get("revisions", {}).items():
            for package_id, package in contents.get("packages", {}).items():
                yield f"{reference}#{revision}:{package_id}", package


def check_package_contents(version: str) -> None:
    """Refuse to publish a package that a server could not be installed from."""
    checked = 0
    for reference, package in cached_packages(version):
        folder = package_folder(reference)
        settings = package.get("info", {}).get("settings", {})
        missing = [name for name in required_files(settings) if not (folder / name).exists()]
        if missing:
            raise VoltmodError(
                f"{reference} is missing {', '.join(missing)}; "
                "check the install() rules in CMakeLists.txt"
            )
        checked += 1
    if not checked:
        raise VoltmodError(f"no {FRAMEWORK_PACKAGE}/{version} package in the local cache to check")


def package_folder(reference: str) -> Path:
    return Path(run_conan_json("cache", "path", reference)["cache_path"])


def framework_version(root: Path) -> str:
    """The package version, read through Conan, which owns that metadata."""
    version = run_conan_json("inspect", root).get("version")
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
