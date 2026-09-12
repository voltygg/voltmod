"""The Conan side of packaging: what this repository publishes, and how it is built."""

import json
import os
from enum import StrEnum
from pathlib import Path

from ... import tools

ROOT = Path.cwd()

# Package names in dependency order: the SDKs, then the framework that consumes them.
SDK_PACKAGES = ("metamod-source", "hl2sdk-cs2", "sqlpp23")
FRAMEWORK_PACKAGE = "voltmod"

# Their package ID is platform-neutral, so only one runner may publish a revision.
HEADER_ONLY_PACKAGES = frozenset({"metamod-source", "sqlpp23"})


class Database(StrEnum):
    OFF = "off"
    ON = "on"
    BOTH = "both"

    def option_values(self) -> tuple[str, ...]:
        """The `with_database` values this selector builds."""
        if self is Database.BOTH:
            return ("False", "True")
        return ("True" if self is Database.ON else "False",)


def run(*args: str, capture: bool = False) -> str:
    result = tools.run_tool("conan", *args, capture=capture)
    return result.stdout or ""


def create(recipe: Path, *args: str) -> None:
    """Create one package with the release profile, the same way a build resolves it."""
    profile, settings = tools.host_profile(ROOT, tools.default_preset())
    if tools.WINDOWS:
        # The runner's own cl, rather than the newest the profile names.
        settings = [*settings, "-s", f"compiler.version={tools.msvc_version()}"]
    run("create", str(recipe), "--profile:all", str(profile), *settings, *args)


def upload(pattern: str) -> None:
    """Upload one pattern, retrying as Conan does."""
    retry = ("-cc", "core.upload:retry=3", "-cc", "core.upload:retry_wait=10")
    run("upload", pattern, "-r", tools.CONAN_REMOTE, "--confirm", *retry)


def login() -> None:
    user = os.environ.get("CLOUDSMITH_USERNAME")
    key = os.environ.get("CLOUDSMITH_API_KEY")
    if not user or not key:
        tools.abort("CLOUDSMITH_USERNAME and CLOUDSMITH_API_KEY are required to publish")
    tools.ensure_remote()
    run("remote", "login", tools.CONAN_REMOTE, user, "-p", key)


def published(name: str, version: str) -> bool:
    """Whether the remote already carries this version."""
    out = tools.run_tool(
        "conan", "list", f"{name}/{version}", "-r", tools.CONAN_REMOTE,
        "--format=json", capture=True, check=False,
    )
    return out.returncode == 0 and f'"{name}/' in out.stdout


def build_sdks() -> None:
    # sqlpp23's client libraries come from conancenter and rarely have prebuilt binaries.
    for name in SDK_PACKAGES:
        create(ROOT / "recipes" / name, "--build=missing")


def build_framework(use_lockfile: bool, database: Database) -> None:
    args = ["--build=missing", *tools.SDK_BUILD_EXCLUSIONS]
    if not use_lockfile:
        args.append("--lockfile=")
    for value in database.option_values():
        create(ROOT, *args, "-o", f"voltmod/*:with_database={value}")


def framework_version() -> str:
    """Read the package version through Conan, which owns that metadata."""
    metadata = json.loads(run("inspect", str(ROOT), "--format=json", capture=True))
    version = metadata.get("version")
    if not version:
        tools.abort("the voltmod Conan recipe has no version")
    return version


def check_release_tag() -> None:
    """A v* tag must agree with the Conan package version."""
    ref = os.environ.get("GITHUB_REF_NAME", "")
    if not ref.startswith("v"):
        return
    declared = framework_version()
    if ref[1:] != declared:
        tools.abort(f"tag {ref} does not match conanfile.py ({declared})")
