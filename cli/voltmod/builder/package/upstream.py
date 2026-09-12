"""Follow the SDK upstreams: resolve a branch tip, and pin it in the recipe's conandata.yml."""

import os
import subprocess
from enum import StrEnum

import yaml

from ... import tools
from . import conan

# The branches watch() follows, and how each package's version is spelled.
UPSTREAM = {
    "hl2sdk-cs2": {
        "url": "https://github.com/alliedmodders/hl2sdk.git",
        "branch": "cs2",
        "scheme": "date",  # 2026.07.23
    },
    "metamod-source": {
        "url": "https://github.com/alliedmodders/metamod-source.git",
        "branch": "master",
        "scheme": "metamod",  # 2.0.0.20260711
    },
}


class UpstreamPackage(StrEnum):
    HL2SDK = "hl2sdk-cs2"
    METAMOD = "metamod-source"


def _branch_tip(url: str, branch: str) -> str:
    out = subprocess.run(
        ["git", "ls-remote", url, f"refs/heads/{branch}"],
        check=True, text=True, capture_output=True,
    ).stdout
    if not out.strip():
        tools.abort(f"{url} has no branch {branch}")
    return out.split()[0]


def _commit_day(url: str, commit: str) -> str:
    repo = url.removeprefix("https://github.com/").removesuffix(".git")
    committed = subprocess.run(
        ["gh", "api", f"repos/{repo}/commits/{commit}", "--jq", ".commit.committer.date"],
        check=True, text=True, capture_output=True,
    ).stdout
    return committed.split("T")[0]


def _next_version(name: str, day: str, scheme: str) -> str:
    """The version this commit's date implies, suffixed if the remote already has it."""
    base = day.replace("-", ".") if scheme == "date" else f"2.0.0.{day.replace('-', '')}"
    version, n = base, 0
    while conan.published(name, version):
        n += 1
        version = f"{base}.{n}"
    return version


def recipe_version(name: str) -> str:
    """The single version of record: the one key under conandata.yml's sources."""
    path = conan.ROOT / "recipes" / name / "conandata.yml"
    sources = yaml.safe_load(path.read_text(encoding="utf-8"))["sources"]
    if len(sources) != 1:
        tools.abort(f"recipes/{name}/conandata.yml must pin exactly one version")
    return next(iter(sources))


def follow(package: UpstreamPackage | None) -> None:
    """Rewrite conandata.yml for every selected package whose upstream branch has moved."""
    changed = False
    for name in (package.value,) if package else UPSTREAM:
        spec = UPSTREAM[name]
        path = conan.ROOT / "recipes" / name / "conandata.yml"
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        current = next(iter(data["sources"].values()))["commit"]

        tip = _branch_tip(spec["url"], spec["branch"])
        if current == tip:
            print(f"{name}: already at {tip[:12]}")
            continue

        version = _next_version(name, _commit_day(spec["url"], tip), spec["scheme"])
        # Keep date-like version keys quoted and the generated diff stable.
        path.write_text(
            f'sources:\n  "{version}":\n    url: "{spec["url"]}"\n    commit: "{tip}"\n',
            encoding="utf-8",
            newline="\n",
        )
        print(f"{name}: {current[:12]} -> {tip[:12]} as {version}")
        _emit_output(f"{name}-version", version)
        _emit_output(f"{name}-old", current)
        changed = True

    _emit_output("changed", "true" if changed else "false")


def _emit_output(key: str, value: str) -> None:
    """Hand a value to the surrounding GitHub Actions step, when there is one."""
    if path := os.environ.get("GITHUB_OUTPUT"):
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(f"{key}={value}\n")
