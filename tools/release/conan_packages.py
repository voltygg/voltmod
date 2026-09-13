"""The Conan packages this repository publishes, and how each is built and uploaded."""

import os
from enum import StrEnum
from pathlib import Path

from voltmod.conan import REMOTE, SDK_BUILD_EXCLUSIONS, ensure_remote, profile_args, run_conan_json
from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, msvc_version, run_tool
from voltmod.project import default_preset

# Dependency order: the SDKs, then the framework that consumes them.
SDK_PACKAGES = ("metamod-source", "hl2sdk-cs2", "sqlpp23")
FRAMEWORK_PACKAGE = "voltmod"

# Their package id is platform-neutral, so only one runner may publish a revision.
HEADER_ONLY_PACKAGES = frozenset({"metamod-source", "sqlpp23"})


class DatabaseVariants(StrEnum):
    OFF = "off"
    ON = "on"
    BOTH = "both"

    def option_values(self) -> tuple[str, ...]:
        """The `with_database` values this choice builds."""
        if self is DatabaseVariants.BOTH:
            return ("False", "True")
        return ("True" if self is DatabaseVariants.ON else "False",)


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


def build_framework(root: Path, *, use_lockfile: bool, database: DatabaseVariants) -> None:
    args = ["--build=missing", *SDK_BUILD_EXCLUSIONS]
    if not use_lockfile:
        args.append("--lockfile=")
    for value in database.option_values():
        create_package(root, root, *args, "-o", f"voltmod/*:with_database={value}")


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
