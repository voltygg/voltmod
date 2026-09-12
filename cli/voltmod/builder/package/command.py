"""The `voltmod package` commands."""

import os
import subprocess
from enum import StrEnum
from typing import Annotated

import typer

from ... import tools
from . import cloudsmith, conan, upstream
from .conan import Database

app = typer.Typer(help="Build, publish, and maintain VoltMod's Conan packages.")


class BuildTarget(StrEnum):
    SDK = "sdk"
    FRAMEWORK = "framework"
    ALL = "all"


Target = Annotated[BuildTarget, typer.Argument(help="Which packages to act on")]
NoLockfile = Annotated[
    bool,
    typer.Option("--no-lockfile", help="Build VoltMod without the lockfile after an SDK bump"),
]
DatabaseOption = Annotated[
    Database,
    typer.Option("--database", help="Which with_database variants to build; one per CI job"),
]


@app.command()
def build(
    target: Target = BuildTarget.ALL,
    no_lockfile: NoLockfile = False,
    database: DatabaseOption = Database.BOTH,
) -> None:
    """Create packages locally."""
    if target in (BuildTarget.SDK, BuildTarget.ALL):
        conan.build_sdks()
    if target in (BuildTarget.FRAMEWORK, BuildTarget.ALL):
        conan.build_framework(use_lockfile=not no_lockfile, database=database)


@app.command()
def publish(
    target: Target = BuildTarget.ALL,
    no_lockfile: NoLockfile = False,
    database: DatabaseOption = Database.BOTH,
) -> None:
    """Create and upload the packages this repository owns."""
    conan.login()
    if target in (BuildTarget.SDK, BuildTarget.ALL):
        conan.build_sdks()
        for name in conan.SDK_PACKAGES:
            if name in conan.HEADER_ONLY_PACKAGES and tools.WINDOWS:
                continue
            conan.upload(f"{name}/*")
    if target in (BuildTarget.FRAMEWORK, BuildTarget.ALL):
        conan.check_release_tag()
        conan.build_framework(use_lockfile=not no_lockfile, database=database)
        conan.upload(f"{conan.FRAMEWORK_PACKAGE}/*")


@app.command("version")
def show_version() -> None:
    """Print the VoltMod Conan package version for scripts and workflows."""
    print(conan.framework_version())


@app.command()
def tag() -> None:
    """Tag this commit with each recipe's pinned version for provenance."""
    for name in conan.SDK_PACKAGES:
        label = f"sdk/{name}/{upstream.recipe_version(name)}"
        created = subprocess.run(["git", "tag", label], capture_output=True, text=True)
        if created.returncode:
            print(f"{label} already exists")
            continue
        subprocess.run(["git", "push", "origin", label], check=True)
        print(f"tagged {label}")


@app.command()
def prune(
    keep: Annotated[int, typer.Option("--keep", help="Versions to keep per package")] = 3,
    dry_run: Annotated[bool, typer.Option("--dry-run", help="Report without deleting")] = False,
) -> None:
    """Delete artifacts no consumer can resolve."""
    token = os.environ.get("CLOUDSMITH_API_KEY", "")
    if not token and not dry_run:
        tools.abort("CLOUDSMITH_API_KEY is required to delete")
    cloudsmith.prune(keep, token, dry_run)


@app.command()
def watch(
    package: Annotated[
        upstream.UpstreamPackage | None,
        typer.Option("--package", help="Just this package (default: both)"),
    ] = None,
) -> None:
    """Rewrite conandata.yml when an upstream branch has moved."""
    upstream.follow(package)
