import os
from collections.abc import Iterator
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError
from voltmod.platforms import Platform
from voltmod.project import default_preset
from voltmod.server.install import HOST_GAMEDATA, host_binary, loader_binary
from voltmod.toolchain.conan import (
    PACKAGE_REMOTE,
    PREBUILT_SDK_ARGS,
    conan_json,
    ensure_remote,
    profile_args,
)
from voltmod.toolchain.msvc import msvc_version
from voltmod.toolchain.process import WINDOWS, run_tool

# In dependency order.
SDK_PACKAGES = ("khook", "hl2sdk-cs2", "sqlpp23")
FRAMEWORK_PACKAGE = "voltmod"

# Their package id is platform-neutral, so only the Linux runner uploads them.
HEADER_ONLY_PACKAGES = frozenset({"sqlpp23"})


def log_in(root: Path) -> None:
    user = os.environ.get("CLOUDSMITH_USERNAME")
    key = os.environ.get("CLOUDSMITH_API_KEY")
    if not user or not key:
        raise VoltmodError("CLOUDSMITH_USERNAME and CLOUDSMITH_API_KEY are required to publish")
    ensure_remote(root)
    run_tool("conan", "remote", "login", PACKAGE_REMOTE, user, "-p", key)


def build_sdks(root: Path) -> None:
    # sqlpp23's database clients come from conancenter, which rarely has binaries for them.
    for name in SDK_PACKAGES:
        _create(root, root / "recipes" / name, "--build=missing")


def upload_sdks() -> None:
    for name in SDK_PACKAGES:
        if not (WINDOWS and name in HEADER_ONLY_PACKAGES):
            _upload(f"{name}/*")


def build_framework(root: Path, *, use_lockfile: bool) -> None:
    args = ["--build=missing", *PREBUILT_SDK_ARGS]
    if not use_lockfile:
        args.append("--lockfile=")
    _create(root, root, *args)
    _check_contents(framework_version(root))


def upload_framework(root: Path) -> None:
    # A restored CI cache can hold other revisions; upload only the one just built.
    _upload(f"{FRAMEWORK_PACKAGE}/{framework_version(root)}#latest")


def framework_version(root: Path) -> str:
    version = conan_json("inspect", root).get("version")
    if not version:
        raise VoltmodError("the voltmod Conan recipe has no version")
    return version


def check_release_tag(root: Path) -> None:
    """Fail when a v* tag disagrees with the version in conanfile.py."""
    tag = os.environ.get("GITHUB_REF_NAME", "")
    if not tag.startswith("v"):
        return
    declared = framework_version(root)
    if tag[1:] != declared:
        raise VoltmodError(f"tag {tag} does not match conanfile.py ({declared})")


def is_published(name: str, version: str) -> bool:
    listing = conan_json("list", f"{name}/{version}", "-r", PACKAGE_REMOTE).get(PACKAGE_REMOTE, {})
    return bool(references(listing, name))


def references(listing: dict[str, Any], name: str) -> dict[str, Any]:
    """The listing's entries for `name`, without the "error" entry `conan list` gives for a miss."""
    return {
        reference: body for reference, body in listing.items() if reference.startswith(f"{name}/")
    }


def _create(root: Path, recipe: Path, *args: str) -> None:
    settings = profile_args(root, default_preset())
    if WINDOWS:
        # The runner's own cl, not the newest one the profile names.
        settings += ["-s", f"compiler.version={msvc_version()}"]
    run_tool("conan", "create", recipe, *settings, *args)


def _upload(pattern: str) -> None:
    retry = ("-cc", "core.upload:retry=3", "-cc", "core.upload:retry_wait=10")
    run_tool("conan", "upload", pattern, "-r", PACKAGE_REMOTE, "--confirm", *retry)


def _check_contents(version: str) -> None:
    """Refuse a package a server could not be installed from."""
    packages = list(_cached_packages(version))
    if not packages:
        raise VoltmodError(f"no {FRAMEWORK_PACKAGE}/{version} package in the local cache to check")
    for reference, settings in packages:
        folder = Path(conan_json("cache", "path", reference)["cache_path"])
        missing = [name for name in _required_files(settings) if not (folder / name).exists()]
        if missing:
            raise VoltmodError(
                f"{reference} is missing {', '.join(missing)}; "
                "check the install() rules in CMakeLists.txt"
            )


def _cached_packages(version: str) -> Iterator[tuple[str, dict[str, Any]]]:
    """Each full package reference in the local cache, with its settings."""
    listing = conan_json("list", f"{FRAMEWORK_PACKAGE}/{version}#latest:*", "-c").get(
        "Local Cache", {}
    )
    for reference, body in references(listing, FRAMEWORK_PACKAGE).items():
        for revision, contents in body.get("revisions", {}).items():
            for package_id, package in contents.get("packages", {}).items():
                settings = package.get("info", {}).get("settings", {})
                yield f"{reference}#{revision}:{package_id}", settings


def _required_files(settings: dict[str, Any]) -> tuple[str, ...]:
    windows = settings.get("os") == "Windows"
    platform = Platform.WINDOWS if windows else Platform.LINUX
    prefix, suffix = ("", ".lib") if windows else ("lib", ".a")
    return (
        f"lib/{prefix}voltmod-portable{suffix}",
        f"lib/{prefix}voltmod-sdk{suffix}",
        f"lib/{prefix}voltmod-database{suffix}",
        # Nothing links the host or the loader; a packaging mistake only shows on a live server.
        host_binary(platform),
        loader_binary(platform),
        HOST_GAMEDATA,
        "include/VoltMod/Api.hpp",
        "cmake/VoltModPlugin.cmake",
    )
