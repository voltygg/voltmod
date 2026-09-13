"""Framework release automation: `python -m tools.release <command>` from the repo root."""

import os
from enum import StrEnum
from typing import Annotated

import typer

from tools.release.cloudsmith import prune
from tools.release.conan_packages import (
    FRAMEWORK_PACKAGE,
    HEADER_ONLY_PACKAGES,
    SDK_PACKAGES,
    DatabaseVariants,
    build_framework,
    build_sdks,
    check_release_tag,
    framework_version,
    log_in,
    upload_packages,
)
from tools.release.sdk_updates import SdkPackage, recipe_version, update_sdk_pins
from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, run_tool, run_with_error_messages
from voltmod.project import Project

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
Database = Annotated[
    DatabaseVariants,
    typer.Option("--database", help="Which with_database variants to build; one per CI job"),
]


@app.command()
def build(
    target: Target = Packages.ALL,
    no_lockfile: NoLockfile = False,
    database: Database = DatabaseVariants.BOTH,
) -> None:
    """Create the packages locally."""
    root = Project.load().root
    if target in (Packages.SDK, Packages.ALL):
        build_sdks(root)
    if target in (Packages.FRAMEWORK, Packages.ALL):
        build_framework(root, use_lockfile=not no_lockfile, database=database)


@app.command()
def publish(
    target: Target = Packages.ALL,
    no_lockfile: NoLockfile = False,
    database: Database = DatabaseVariants.BOTH,
) -> None:
    """Create and upload the packages this repository owns."""
    root = Project.load().root
    log_in(root)
    if target in (Packages.SDK, Packages.ALL):
        build_sdks(root)
        for name in SDK_PACKAGES:
            if name in HEADER_ONLY_PACKAGES and WINDOWS:
                continue
            upload_packages(f"{name}/*")
    if target in (Packages.FRAMEWORK, Packages.ALL):
        check_release_tag(root)
        build_framework(root, use_lockfile=not no_lockfile, database=database)
        # A restored CI cache can hold other voltmod revisions; upload only the one just built.
        upload_packages(f"{FRAMEWORK_PACKAGE}/{framework_version(root)}#latest")


@app.command()
def version() -> None:
    """Print the VoltMod Conan package version for scripts and workflows."""
    print(framework_version(Project.load().root))


@app.command()
def tag() -> None:
    """Tag this commit with each SDK recipe's pinned version."""
    root = Project.load().root
    for name in SDK_PACKAGES:
        label = f"sdk/{name}/{recipe_version(root, name)}"
        created = run_tool("git", "tag", label, capture=True, check=False)
        if created.returncode:
            print(f"{label} already exists")
            continue
        run_tool("git", "push", "origin", label)
        print(f"tagged {label}")


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
    """Rewrite conandata.yml when an upstream branch has moved."""
    update_sdk_pins(Project.load().root, package)


run_with_error_messages(app)
