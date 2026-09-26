import os
from dataclasses import dataclass
from datetime import date
from enum import StrEnum
from pathlib import Path

import yaml

from tools.release.conan_packages import is_published
from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.toolchain.process import run


class SdkPackage(StrEnum):
    HL2SDK = "hl2sdk-cs2"
    KHOOK = "khook"


@dataclass(frozen=True)
class Upstream:
    url: str
    branch: str
    # Formats the tip's commit date into the package version.
    version_format: str


UPSTREAMS = {
    SdkPackage.HL2SDK: Upstream(
        "https://github.com/alliedmodders/hl2sdk.git", "cs2", "{:%Y.%m.%d}"
    ),
    SdkPackage.KHOOK: Upstream("https://github.com/Kenzzer/KHook.git", "master", "{:%Y.%m.%d}"),
}


def recipe_version(root: Path, name: str) -> str:
    return _pinned_source(root, name)[0]


def update_sdk_pins(root: Path, package: SdkPackage | None) -> None:
    """Pin each selected package to its upstream branch tip, and tell the workflow if any moved."""
    changed = False
    for name in (package,) if package else UPSTREAMS:
        upstream = UPSTREAMS[name]
        current = _pinned_source(root, name)[1]
        tip = _branch_tip(upstream)
        if current == tip:
            console.info(f"{name}: already at {tip[:12]}")
            continue

        version = _next_version(name, upstream.version_format.format(_commit_date(upstream, tip)))
        # Quoted, so a date-like version stays a string.
        (root / "recipes" / name / "conandata.yml").write_text(
            f'sources:\n  "{version}":\n    url: "{upstream.url}"\n    commit: "{tip}"\n',
            encoding="utf-8",
            newline="\n",
        )
        console.done(f"{name}: {current[:12]} -> {tip[:12]} as {version}")
        changed = True

    if output := os.environ.get("GITHUB_OUTPUT"):
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(f"changed={str(changed).lower()}\n")


def _pinned_source(root: Path, name: str) -> tuple[str, str]:
    """The version and commit a recipe's conandata.yml pins."""
    conandata = root / "recipes" / name / "conandata.yml"
    sources = yaml.safe_load(conandata.read_text(encoding="utf-8"))["sources"]
    if len(sources) != 1:
        raise VoltmodError(f"recipes/{name}/conandata.yml must pin exactly one version")
    [(version, source)] = sources.items()
    return version, source["commit"]


def _branch_tip(upstream: Upstream) -> str:
    output = run("git", "ls-remote", upstream.url, f"refs/heads/{upstream.branch}", capture=True)
    if not output.stdout.strip():
        raise VoltmodError(f"{upstream.url} has no branch {upstream.branch}")
    return output.stdout.split()[0]


def _commit_date(upstream: Upstream, commit: str) -> date:
    repository = upstream.url.removeprefix("https://github.com/").removesuffix(".git")
    query = ("gh", "api", f"repos/{repository}/commits/{commit}", "--jq", ".commit.committer.date")
    return date.fromisoformat(run(*query, capture=True).stdout[:10])


def _next_version(name: str, base: str) -> str:
    """`base`, suffixed .1, .2, ... until the remote does not have it."""
    version, suffix = base, 0
    while is_published(name, version):
        suffix += 1
        version = f"{base}.{suffix}"
    return version
