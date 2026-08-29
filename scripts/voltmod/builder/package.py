"""Build, publish, prune, and update VoltMod's Conan packages."""

import json
import os
import subprocess
import urllib.request
from enum import StrEnum
from pathlib import Path
from typing import Annotated

import typer
import yaml

from .. import tools

ROOT = Path.cwd()

# Package names in dependency order: the SDKs, then the framework that consumes them.
SDK_PACKAGES = ("metamod-source", "hl2sdk-cs2")
KIT_PACKAGE = "voltmod"

# Upstream branches watch() follows, and how each package's version is spelled.
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

CLOUDSMITH_OWNER, CLOUDSMITH_REPO = "volty", "voltmod"
CLOUDSMITH_API = f"https://api.cloudsmith.io/v1/packages/{CLOUDSMITH_OWNER}/{CLOUDSMITH_REPO}/"

app = typer.Typer(help="Build, publish, and maintain VoltMod's Conan packages.")


class BuildTarget(StrEnum):
    SDK = "sdk"
    KIT = "kit"
    ALL = "all"


class UpstreamPackage(StrEnum):
    HL2SDK = "hl2sdk-cs2"
    METAMOD = "metamod-source"


Target = Annotated[BuildTarget, typer.Argument(help="Which packages to act on")]
NoLockfile = Annotated[
    bool,
    typer.Option("--no-lockfile", help="Build VoltMod without the lockfile after an SDK bump"),
]


def _conan(*args: str, capture: bool = False) -> str:
    result = tools.run_tool("conan", *args, capture=capture)
    return result.stdout or ""


def _profile(name: str) -> Path:
    profile = ROOT / "conan/profiles" / f"{name}.txt"
    if not profile.is_file():
        tools.die(f"no Conan profile at {profile}")
    return profile


def _host_settings() -> tuple[str, list[str]]:
    """Return the host profile and runner-specific settings."""
    if tools.WINDOWS:
        version = tools.msvc_version()
        return "windows-msvc", [
            "-s",
            "compiler.runtime_type=Release",
            "-s",
            f"compiler.version={version}",
        ]
    return "linux-steamrt", []


def _create(
    recipe: Path,
    *,
    extra: list[str] | None = None,
    options: list[str] | None = None,
) -> None:
    profile_name, settings = _host_settings()
    _conan(
        "create",
        str(recipe),
        "--profile:all",
        str(_profile(profile_name)),
        "-s",
        "build_type=Release",
        *settings,
        *(options or ()),
        *(extra or ()),
    )


def _upload(pattern: str) -> None:
    """Upload one pattern with Conan's built-in retry support."""
    _conan(
        "upload",
        pattern,
        "-r",
        tools.CONAN_REMOTE,
        "--confirm",
        "-cc",
        "core.upload:retry=3",
        "-cc",
        "core.upload:retry_wait=10",
    )


def _recipe_version(name: str) -> str:
    """The single version of record: the one key under conandata.yml's sources."""
    data = yaml.safe_load((ROOT / "recipes" / name / "conandata.yml").read_text(encoding="utf-8"))
    sources = data["sources"]
    if len(sources) != 1:
        tools.die(f"recipes/{name}/conandata.yml must pin exactly one version")
    return next(iter(sources))


def _kit_version() -> str:
    """Read the package version through Conan, which owns that metadata."""
    metadata = json.loads(_conan("inspect", str(ROOT), "--format=json", capture=True))
    version = metadata.get("version")
    if not version:
        tools.die("the voltmod Conan recipe has no version")
    return version


def _build_sdks() -> None:
    for name in SDK_PACKAGES:
        _create(ROOT / "recipes" / name)


def _build_kit(use_lockfile: bool) -> None:
    extra = ["--build=missing", *tools.SDK_BUILD_EXCLUSIONS]
    if not use_lockfile:
        extra.append("--lockfile=")
    for postgres in ("False", "True"):
        _create(
            ROOT,
            extra=extra,
            options=["-o", f"voltmod/*:with_postgres={postgres}"],
        )


@app.command()
def build(
    target: Target = BuildTarget.ALL,
    no_lockfile: NoLockfile = False,
) -> None:
    """Create packages locally."""
    if target in (BuildTarget.SDK, BuildTarget.ALL):
        _build_sdks()
    if target in (BuildTarget.KIT, BuildTarget.ALL):
        _build_kit(use_lockfile=not no_lockfile)


@app.command()
def publish(
    target: Target = BuildTarget.ALL,
    no_lockfile: NoLockfile = False,
) -> None:
    """Create and upload the packages this repository owns."""
    _login()
    if target in (BuildTarget.SDK, BuildTarget.ALL):
        _build_sdks()
        for name in SDK_PACKAGES:
            # Its platform-neutral package ID must only receive one published revision.
            if name == "metamod-source" and tools.WINDOWS:
                continue
            _upload(f"{name}/*")
    if target in (BuildTarget.KIT, BuildTarget.ALL):
        _check_release_tag()
        _build_kit(use_lockfile=not no_lockfile)
        _upload(f"{KIT_PACKAGE}/*")


def _check_release_tag() -> None:
    """A v* tag must agree with the Conan package version."""
    ref = os.environ.get("GITHUB_REF_NAME", "")
    if not ref.startswith("v"):
        return
    declared = _kit_version()
    if ref[1:] != declared:
        tools.die(f"tag {ref} does not match conanfile.py ({declared})")


@app.command("version")
def show_version() -> None:
    """Print the VoltMod Conan package version for scripts and workflows."""
    print(_kit_version())


@app.command()
def tag() -> None:
    """Tag this commit with each recipe's pinned version for provenance."""
    for name in SDK_PACKAGES:
        label = f"sdk/{name}/{_recipe_version(name)}"
        created = subprocess.run(["git", "tag", label], capture_output=True, text=True)
        if created.returncode:
            print(f"{label} already exists")
            continue
        subprocess.run(["git", "push", "origin", label], check=True)
        print(f"tagged {label}")


def _login() -> None:
    user = os.environ.get("CLOUDSMITH_USERNAME")
    key = os.environ.get("CLOUDSMITH_API_KEY")
    if not user or not key:
        tools.die("CLOUDSMITH_USERNAME and CLOUDSMITH_API_KEY are required to publish")
    tools.ensure_remote()
    _conan("remote", "login", tools.CONAN_REMOTE, user, "-p", key)


# Cloudsmith cannot delete revisions through Conan, so pruning uses its REST API.


def _newest(entries: dict) -> str:
    return max(entries, key=lambda key: entries[key].get("timestamp", 0))


def _reachable_revisions(keep_versions: int) -> set[str]:
    """Every recipe and package revision a consumer can still resolve."""
    keep: set[str] = set()
    for name in (*SDK_PACKAGES, KIT_PACKAGE):
        listing = json.loads(
            _conan(
                "list",
                f"{name}/*#*:*#*",
                "-r",
                tools.CONAN_REMOTE,
                "--format=json",
                capture=True,
            )
        ).get(tools.CONAN_REMOTE, {})

        versions: dict[str, dict] = {}
        for ref, body in listing.items():
            revisions = body.get("revisions")
            if revisions is None:
                tools.die(f"unexpected conan list output for {ref}: no 'revisions' key")
            versions.setdefault(ref.split("/", 1)[1], {}).update(revisions)

        for version in sorted(versions)[-keep_versions:]:
            recipe_rev = _newest(versions[version])
            keep.add(recipe_rev)
            for body in versions[version][recipe_rev].get("packages", {}).values():
                if package_revs := body.get("revisions", {}):
                    keep.add(_newest(package_revs))
    return keep


def _cloudsmith(method: str, url: str, token: str) -> bytes:
    request = urllib.request.Request(url, method=method, headers={"X-Api-Key": token})
    with urllib.request.urlopen(request) as response:
        return response.read()


@app.command()
def prune(
    keep: Annotated[
        int,
        typer.Option("--keep", help="Versions to keep per package"),
    ] = 3,
    dry_run: Annotated[
        bool,
        typer.Option("--dry-run", help="Report without deleting"),
    ] = False,
) -> None:
    """Delete artifacts no consumer can resolve."""
    token = os.environ.get("CLOUDSMITH_API_KEY", "")
    if not token and not dry_run:
        tools.die("CLOUDSMITH_API_KEY is required to delete")

    reachable = _reachable_revisions(keep)
    print(f"{len(reachable)} reachable revisions")

    url = CLOUDSMITH_API + "?page_size=500"
    raw = _cloudsmith("GET", url, token) if token else urllib.request.urlopen(url).read()
    rows = json.loads(raw)

    removed = 0
    for row in rows:
        revision = (row.get("identifiers") or {}).get("conan_revision_hash")
        if not revision or revision in reachable:
            continue
        print(f"remove {row['name']}/{row['version']} {row.get('filename')} #{revision[:12]}")
        removed += 1
        if not dry_run:
            _cloudsmith("DELETE", f"{CLOUDSMITH_API}{row['slug_perm']}/", token)

    verb = "would remove" if dry_run else "removed"
    print(f"{verb} {removed} of {len(rows)} artifacts")


def _git_tip(url: str, branch: str) -> str:
    out = subprocess.run(
        ["git", "ls-remote", url, f"refs/heads/{branch}"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    if not out.strip():
        tools.die(f"{url} has no branch {branch}")
    return out.split()[0]


def _gh(*args: str) -> str:
    return subprocess.run(["gh", *args], check=True, text=True, capture_output=True).stdout


def _next_version(name: str, day: str, scheme: str) -> str:
    """The version this commit's date implies, suffixed if the remote already has it."""
    base = day.replace("-", ".") if scheme == "date" else f"2.0.0.{day.replace('-', '')}"
    version, n = base, 0
    while _published(name, version):
        n += 1
        version = f"{base}.{n}"
    return version


def _published(name: str, version: str) -> bool:
    out = tools.run_tool(
        "conan",
        "list",
        f"{name}/{version}",
        "-r",
        tools.CONAN_REMOTE,
        "--format=json",
        capture=True,
        check=False,
    )
    return out.returncode == 0 and f'"{name}/' in out.stdout


@app.command()
def watch(
    package: Annotated[
        UpstreamPackage | None,
        typer.Option("--package", help="Just this package (default: both)"),
    ] = None,
) -> None:
    """Rewrite conandata.yml when an upstream branch has moved."""
    changed = False
    selected = (package.value,) if package else UPSTREAM
    for name in selected:
        spec = UPSTREAM[name]
        data_path = ROOT / "recipes" / name / "conandata.yml"
        data = yaml.safe_load(data_path.read_text(encoding="utf-8"))
        current = next(iter(data["sources"].values()))["commit"]

        tip = _git_tip(spec["url"], spec["branch"])
        if current == tip:
            print(f"{name}: already at {tip[:12]}")
            continue

        repo = spec["url"].removeprefix("https://github.com/").removesuffix(".git")
        committed = _gh("api", f"repos/{repo}/commits/{tip}", "--jq", ".commit.committer.date")
        day = committed.split("T")[0]
        version = _next_version(name, day, spec["scheme"])

        # Keep date-like version keys quoted and the generated diff stable.
        data_path.write_text(
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
