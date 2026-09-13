"""Following the SDK upstreams: find a branch's new tip, and pin it in the recipe's conandata.yml."""

import os
import subprocess
from enum import StrEnum
from pathlib import Path

import yaml

from tools.release.conan_packages import is_published
from voltmod.errors import VoltmodError

# The branches followed, and how each package spells its version.
UPSTREAMS = {
    "hl2sdk-cs2": {
        "url": "https://github.com/alliedmodders/hl2sdk.git",
        "branch": "cs2",
        "version_style": "date",  # 2026.07.23
    },
    "metamod-source": {
        "url": "https://github.com/alliedmodders/metamod-source.git",
        "branch": "master",
        "version_style": "metamod",  # 2.0.0.20260711
    },
}


class SdkPackage(StrEnum):
    HL2SDK = "hl2sdk-cs2"
    METAMOD = "metamod-source"


def recipe_version(root: Path, name: str) -> str:
    """The one version a recipe's conandata.yml pins."""
    sources = _read_conandata(root, name)["sources"]
    if len(sources) != 1:
        raise VoltmodError(f"recipes/{name}/conandata.yml must pin exactly one version")
    return next(iter(sources))


def update_sdk_pins(root: Path, package: SdkPackage | None) -> None:
    """Rewrite conandata.yml for each selected package whose upstream branch has moved."""
    changed = False
    for name in (package.value,) if package else UPSTREAMS:
        upstream = UPSTREAMS[name]
        current = next(iter(_read_conandata(root, name)["sources"].values()))["commit"]

        tip = _branch_tip(upstream["url"], upstream["branch"])
        if current == tip:
            print(f"{name}: already at {tip[:12]}")
            continue

        day = _commit_day(upstream["url"], tip)
        version = _next_version(name, day, upstream["version_style"])
        # Quoted, so a date-like version stays a string and the diff stays stable.
        (root / "recipes" / name / "conandata.yml").write_text(
            f'sources:\n  "{version}":\n    url: "{upstream["url"]}"\n    commit: "{tip}"\n',
            encoding="utf-8",
            newline="\n",
        )
        print(f"{name}: {current[:12]} -> {tip[:12]} as {version}")
        _set_step_output(f"{name}-version", version)
        _set_step_output(f"{name}-old", current)
        changed = True

    _set_step_output("changed", "true" if changed else "false")


def _read_conandata(root: Path, name: str) -> dict:
    return yaml.safe_load((root / "recipes" / name / "conandata.yml").read_text(encoding="utf-8"))


def _branch_tip(url: str, branch: str) -> str:
    output = subprocess.run(
        ["git", "ls-remote", url, f"refs/heads/{branch}"],
        check=True, text=True, capture_output=True,
    ).stdout
    if not output.strip():
        raise VoltmodError(f"{url} has no branch {branch}")
    return output.split()[0]


def _commit_day(url: str, commit: str) -> str:
    repository = url.removeprefix("https://github.com/").removesuffix(".git")
    committed = subprocess.run(
        ["gh", "api", f"repos/{repository}/commits/{commit}", "--jq", ".commit.committer.date"],
        check=True, text=True, capture_output=True,
    ).stdout
    return committed.split("T")[0]


def _next_version(name: str, day: str, version_style: str) -> str:
    """The version a commit's date implies, suffixed when the remote already has it."""
    if version_style == "date":
        base = day.replace("-", ".")
    else:
        base = f"2.0.0.{day.replace('-', '')}"
    version, suffix = base, 0
    while is_published(name, version):
        suffix += 1
        version = f"{base}.{suffix}"
    return version


def _set_step_output(key: str, value: str) -> None:
    """Hand a value to the surrounding GitHub Actions step, when there is one."""
    if path := os.environ.get("GITHUB_OUTPUT"):
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(f"{key}={value}\n")
