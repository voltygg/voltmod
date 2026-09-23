import os
from enum import StrEnum
from typing import Annotated

import typer

from tools.release.cloudsmith import prune
from tools.release.conan_packages import (
    SDK_PACKAGES,
    build_framework,
    build_sdks,
    check_release_tag,
    framework_version,
    log_in,
    upload_framework,
    upload_sdks,
)
from tools.release.sdk_updates import SdkPackage, recipe_version, update_sdk_pins
from voltmod import console
from voltmod.cli import run_cli
from voltmod.errors import VoltmodError
from voltmod.project import Project
from voltmod.toolchain.process import run

app = typer.Typer(
    help="Build, publish, and maintain VoltMod's Conan packages.", no_args_is_help=True
)


class Packages(StrEnum):
    SDK = "sdk"
    FRAMEWORK = "framework"
    ALL = "all"


Target = Annotated[Packages, typer.Argument(help="Which packages to act on")]
NoLockfile = Annotated[
    bool, typer.Option("--no-lockfile", help="Build VoltMod without the lockfile after an SDK bump")
]


@app.command()
def build(target: Target = Packages.ALL, no_lockfile: NoLockfile = False) -> None:
    """Create the packages locally."""
    root = Project.load().root
    if target in (Packages.SDK, Packages.ALL):
        build_sdks(root)
    if target in (Packages.FRAMEWORK, Packages.ALL):
        build_framework(root, use_lockfile=not no_lockfile)


@app.command()
def publish(target: Target = Packages.ALL, no_lockfile: NoLockfile = False) -> None:
    """Create and upload the packages this repository owns."""
    root = Project.load().root
    log_in(root)
    if target in (Packages.SDK, Packages.ALL):
        build_sdks(root)
        upload_sdks()
    if target in (Packages.FRAMEWORK, Packages.ALL):
        check_release_tag(root)
        build_framework(root, use_lockfile=not no_lockfile)
        upload_framework(root)


@app.command()
def version() -> None:
    """Print the VoltMod Conan package version."""
    console.info(framework_version(Project.load().root))


@app.command()
def tag() -> None:
    """Tag this commit with each SDK recipe's pinned version."""
    root = Project.load().root
    for name in SDK_PACKAGES:
        label = f"sdk/{name}/{recipe_version(root, name)}"
        # CI checks out without tags, so ask the remote.
        remote = run("git", "ls-remote", "--tags", "origin", f"refs/tags/{label}", capture=True)
        if remote.stdout.strip():
            console.info(f"{label} already exists")
            continue
        run("git", "tag", label)
        run("git", "push", "origin", label)
        console.done(f"tagged {label}")


@app.command("prune")
def prune_command(
    keep: Annotated[int, typer.Option("--keep", help="Versions to keep per package")] = 3,
    dry_run: Annotated[bool, typer.Option("--dry-run", help="Report without deleting")] = False,
) -> None:
    """Delete artifacts no consumer can resolve."""
    Project.load()
    token = os.environ.get("CLOUDSMITH_API_KEY", "")
    if not token and not dry_run:
        raise VoltmodError("CLOUDSMITH_API_KEY is required to delete")
    prune(keep, token, dry_run)


@app.command()
def watch(
    package: Annotated[
        SdkPackage | None, typer.Option("--package", help="Just this package (default: both)")
    ] = None,
) -> None:
    """Pin the SDK recipes to their upstream branch tips."""
    update_sdk_pins(Project.load().root, package)


run_cli(app)
