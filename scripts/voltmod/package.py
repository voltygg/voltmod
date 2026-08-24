"""Release plumbing for the Conan packages this repo owns.

`voltmod package <build|publish|prune|watch>`. All four used to live inside workflow
YAML - three divergent `conan create` blocks, two different upload retry loops, three
copies of the vswhere MSVC probe, and a 126-line Python program embedded in a heredoc
that nothing could lint or run locally. They are ordinary code here, and CI is reduced
to triggers, permissions and one call.

Every command targets the kit checkout it is run from: recipes/<name> for the SDK
packages, the repo root for voltmod itself.
"""

import json
import os
import subprocess
import urllib.request
from pathlib import Path

import yaml

from . import buildtools

ROOT = Path.cwd()

# Package names in dependency order: the SDKs, then the kit that consumes them.
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


def _conan(*args: str, capture: bool = False) -> str:
    argv, env = buildtools.resolve_tool("conan")
    if not capture:
        subprocess.run([*argv, *args], check=True, env=env)
        return ""
    out = subprocess.run([*argv, *args], text=True, capture_output=True, env=env)
    if out.returncode:
        buildtools.die(f"conan {' '.join(args)} failed:\n{out.stderr}")
    return out.stdout


def _profile(name: str) -> Path:
    profile = ROOT / "conan/profiles" / f"{name}.txt"
    if not profile.is_file():
        buildtools.die(f"no Conan profile at {profile}")
    return profile


def _host_settings() -> tuple[str, list[str]]:
    """The profile name and the extra settings this runner needs.

    Windows runners ship a newer cl than the checked-in profile names, and a package
    built with the wrong compiler.version gets an id no consumer resolves.
    """
    if buildtools.WINDOWS:
        version = buildtools.msvc_version()
        return "windows-msvc", [
            "-s", "compiler.runtime_type=Release",
            "-s", f"compiler.version={version}",
        ]
    return "linux-steamrt", []


def _create(recipe: Path, extra: list[str], options: list[str]) -> None:
    profile_name, settings = _host_settings()
    _conan(
        "create", str(recipe),
        "--profile:all", str(_profile(profile_name)),
        "-s", "build_type=Release",
        *settings, *options, *extra,
    )


def _upload(pattern: str) -> None:
    """Upload one pattern. Idempotent, so the retry is free; Cloudsmith 5xxs under load."""
    for attempt in (1, 2, 3):
        try:
            _conan("upload", pattern, "-r", buildtools.CONAN_REMOTE, "--confirm",
                   "-cc", "core.upload:retry=3", "-cc", "core.upload:retry_wait=10")
            return
        except subprocess.CalledProcessError:
            print(f"upload of {pattern} failed (attempt {attempt} of 3)")
    buildtools.die(f"could not upload {pattern}")


def _recipe_version(name: str) -> str:
    """The single version of record: the one key under conandata.yml's sources."""
    data = yaml.safe_load((ROOT / "recipes" / name / "conandata.yml").read_text(encoding="utf-8"))
    sources = data["sources"]
    if len(sources) != 1:
        buildtools.die(f"recipes/{name}/conandata.yml must pin exactly one version")
    return next(iter(sources))


def _build_sdks() -> None:
    for name in SDK_PACKAGES:
        _create(ROOT / "recipes" / name, [], [])


def _build_kit(use_lockfile: bool) -> None:
    extra = ["--build=missing", *buildtools.SDK_BUILD_EXCLUSIONS]
    if not use_lockfile:
        extra.append("--lockfile=")
    for postgres in ("False", "True"):
        _create(ROOT, extra, ["-o", f"voltmod/*:with_postgres={postgres}"])


def build(args) -> int:
    """Create packages locally. The CI gate, and the first half of publish."""
    if args.target in ("sdk", "all"):
        _build_sdks()
    if args.target in ("kit", "all"):
        _build_kit(use_lockfile=not args.no_lockfile)
    return 0


def publish(args) -> int:
    """Create, then upload. Only what this repo owns - third-party mirrors are not ours."""
    _login()
    if args.target in ("sdk", "all"):
        _build_sdks()
        for name in SDK_PACKAGES:
            # metamod-source clears its package_id, so one binary serves every platform;
            # uploading from both would write two revisions under the same id. Both build
            # it - the kit needs it in the local cache either way - but Linux publishes.
            if name == "metamod-source" and buildtools.WINDOWS:
                continue
            _upload(f"{name}/*")
    if args.target in ("kit", "all"):
        _check_release_tag()
        _build_kit(use_lockfile=not args.no_lockfile)
        _upload(f"{KIT_PACKAGE}/*")
    return 0


def _check_release_tag() -> None:
    """A v* tag must agree with version.txt, which is what the recipe actually reads."""
    ref = os.environ.get("GITHUB_REF_NAME", "")
    if not ref.startswith("v"):
        return
    declared = (ROOT / "version.txt").read_text(encoding="utf-8").strip()
    if ref[1:] != declared:
        buildtools.die(f"tag {ref} does not match version.txt ({declared})")


def tag(args) -> int:
    """Tag this commit with each recipe's pinned version, for provenance.

    Answers "which commit of this repo produced hl2sdk-cs2/2026.07.23". Existing tags
    are left alone, so a re-run of publish is harmless.
    """
    for name in SDK_PACKAGES:
        label = f"sdk/{name}/{_recipe_version(name)}"
        created = subprocess.run(["git", "tag", label], capture_output=True, text=True)
        if created.returncode:
            print(f"{label} already exists")
            continue
        subprocess.run(["git", "push", "origin", label], check=True)
        print(f"tagged {label}")
    return 0


def _login() -> None:
    user = os.environ.get("CLOUDSMITH_USERNAME")
    key = os.environ.get("CLOUDSMITH_API_KEY")
    if not user or not key:
        buildtools.die("CLOUDSMITH_USERNAME and CLOUDSMITH_API_KEY are required to publish")
    buildtools.ensure_remote()
    _conan("remote", "login", buildtools.CONAN_REMOTE, user, "-p", key)


# --- prune -----------------------------------------------------------------------
#
# `conan remove` cannot do this: Cloudsmith's Conan API does not implement deletion by
# revision and answers "Recipe not found" for a revision `conan list` just returned. So
# conan decides what is still reachable and the Cloudsmith REST API does the deleting,
# matched on identifiers.conan_revision_hash - the recipe revision on export and sources
# artifacts, the package revision on package ones.


def _newest(entries: dict) -> str:
    return max(entries, key=lambda key: entries[key].get("timestamp", 0))


def _reachable_revisions(keep_versions: int) -> set[str]:
    """Every recipe and package revision a consumer can still resolve."""
    keep: set[str] = set()
    for name in (*SDK_PACKAGES, KIT_PACKAGE):
        listing = json.loads(_conan(
            "list", f"{name}/*#*:*#*", "-r", buildtools.CONAN_REMOTE, "--format=json",
            capture=True,
        )).get(buildtools.CONAN_REMOTE, {})

        versions: dict[str, dict] = {}
        for ref, body in listing.items():
            revisions = body.get("revisions")
            if revisions is None:
                buildtools.die(f"unexpected conan list output for {ref}: no 'revisions' key")
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


def prune(args) -> int:
    """Delete artifacts no consumer can resolve any more.

    SDK revisions move several times a month, so superseded artifacts are pure weight.
    """
    token = os.environ.get("CLOUDSMITH_API_KEY", "")
    if not token and not args.dry_run:
        buildtools.die("CLOUDSMITH_API_KEY is required to delete")

    keep = _reachable_revisions(args.keep)
    print(f"{len(keep)} reachable revisions")

    url = CLOUDSMITH_API + "?page_size=500"
    raw = _cloudsmith("GET", url, token) if token else urllib.request.urlopen(url).read()
    rows = json.loads(raw)

    removed = 0
    for row in rows:
        revision = (row.get("identifiers") or {}).get("conan_revision_hash")
        if not revision or revision in keep:
            continue
        print(f"remove {row['name']}/{row['version']} {row.get('filename')} #{revision[:12]}")
        removed += 1
        if not args.dry_run:
            _cloudsmith("DELETE", f"{CLOUDSMITH_API}{row['slug_perm']}/", token)

    verb = "would remove" if args.dry_run else "removed"
    print(f"{verb} {removed} of {len(rows)} artifacts")
    return 0


# --- watch -----------------------------------------------------------------------


def _git_tip(url: str, branch: str) -> str:
    out = subprocess.run(["git", "ls-remote", url, f"refs/heads/{branch}"],
                         check=True, text=True, capture_output=True).stdout
    if not out.strip():
        buildtools.die(f"{url} has no branch {branch}")
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
    argv, env = buildtools.resolve_tool("conan")
    out = subprocess.run(
        [*argv, "list", f"{name}/{version}", "-r", buildtools.CONAN_REMOTE, "--format=json"],
        text=True, capture_output=True, env=env,
    )
    return out.returncode == 0 and f'"{name}/' in out.stdout


def watch(args) -> int:
    """Report upstream movement as GitHub Actions outputs, and rewrite conandata.yml.

    Writes nothing and opens nothing when the pin is already current, so a scheduled run
    on a quiet day is a no-op.
    """
    changed = False
    for name in (args.package,) if args.package else UPSTREAM:
        spec = UPSTREAM[name]
        data_path = ROOT / "recipes" / name / "conandata.yml"
        data = yaml.safe_load(data_path.read_text(encoding="utf-8"))
        current = next(iter(data["sources"].values()))["commit"]

        tip = _git_tip(spec["url"], spec["branch"])
        if current == tip:
            print(f"{name}: already at {tip[:12]}")
            continue

        repo = spec["url"].removeprefix("https://github.com/").removesuffix(".git")
        committed = _gh("api", f"repos/{repo}/commits/{tip}",
                        "--jq", ".commit.committer.date")
        day = committed.split("T")[0]
        version = _next_version(name, day, spec["scheme"])

        # Written out rather than yaml.safe_dump'd: the version key must stay quoted
        # (an unquoted "2026.08" is a float to YAML), and matching the hand-written
        # shape keeps a bump PR a three-line diff.
        data_path.write_text(
            f'sources:\n  "{version}":\n'
            f'    url: "{spec["url"]}"\n'
            f'    commit: "{tip}"\n',
            encoding="utf-8", newline="\n",
        )
        print(f"{name}: {current[:12]} -> {tip[:12]} as {version}")
        _emit_output(f"{name}-version", version)
        _emit_output(f"{name}-old", current)
        changed = True

    _emit_output("changed", "true" if changed else "false")
    return 0


def _emit_output(key: str, value: str) -> None:
    """Hand a value to the surrounding GitHub Actions step, when there is one."""
    if path := os.environ.get("GITHUB_OUTPUT"):
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(f"{key}={value}\n")


def add_parser(subparsers) -> None:
    """Register `voltmod package ...` on the top-level CLI."""
    parser = subparsers.add_parser("package", help="build, publish and prune the Conan packages")
    sub = parser.add_subparsers(dest="package_command", required=True)

    for name, fn, help_text in (
        ("build", build, "conan create the packages this repo owns"),
        ("publish", publish, "create and upload them to the remote"),
    ):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("target", choices=("sdk", "kit", "all"), nargs="?", default="all",
                       help="which packages to act on (default: all)")
        # conan.lock pins the SDK revisions a recipe bump is replacing, so the run that
        # produces those new revisions has to ignore it.
        p.add_argument("--no-lockfile", action="store_true",
                       help="build voltmod without conan.lock")
        p.set_defaults(run=fn)

    p = sub.add_parser("tag", help="tag this commit with each recipe's pinned version")
    p.set_defaults(run=tag)

    p = sub.add_parser("prune", help="delete artifacts no consumer can resolve")
    p.add_argument("--keep", type=int, default=3, help="versions to keep per package")
    p.add_argument("--dry-run", action="store_true", help="report without deleting")
    p.set_defaults(run=prune)

    p = sub.add_parser("watch", help="rewrite conandata.yml when upstream has moved")
    p.add_argument("--package", choices=tuple(UPSTREAM), help="just this one (default: both)")
    p.set_defaults(run=watch)
