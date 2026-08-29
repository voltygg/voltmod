"""A VoltMod checkout consumed in place: the editable dev loop, and the relock for commits."""

import json
from pathlib import Path

from ..tools import SDK_BUILD_EXCLUSIONS, conan_home, die, ensure_msvc_env, host_profile, run_tool


def _voltmod_entry(entries: dict) -> Path | None:
    """The checkout an editable listing registers for `voltmod`, if it names one."""
    for ref, entry in entries.items():
        if ref.startswith("voltmod/"):
            return Path(entry["path"]).parent
    return None


def editable() -> Path | None:
    """The checkout registered as the editable `voltmod` package (`conan editable add`), if any.

    Read from Conan's own registry file rather than `conan editable list`: every build asks this,
    and starting Conan for one boolean costs about a second. A file Conan writes differently than
    expected falls back to asking it, so the answer is never silently "no editable" - a build that
    wrongly skips compiling the checkout links a stale framework.
    """
    registry = conan_home() / "editable_packages.json"
    if not registry.is_file():
        return None
    try:
        return _voltmod_entry(json.loads(registry.read_text(encoding="utf-8")))
    except (OSError, ValueError, AttributeError, KeyError, TypeError):
        listing = run_tool("conan", "editable", "list", "--format=json", capture=True, check=False)
        return None if listing.returncode else _voltmod_entry(json.loads(listing.stdout or "{}"))


def _build_args(repo_root: Path, checkout: Path, preset: str) -> list[str]:
    """Conan arguments that build the checkout the way the plugins consume it.

    The consumer's lock, not the checkout's: the dependency graph is part of the package id, so a
    binary made against other dependency versions is one the plugins never resolve. Partial,
    because the recipe's own test and tool requirements need not be in it.
    """
    profile, settings = host_profile(checkout, preset)
    lock = repo_root / "conan.lock"
    return [
        str(checkout),
        "--profile:all", str(profile), *settings,
        "-o", "voltmod/*:with_postgres=True",
        *([f"--lockfile={lock}", "--lockfile-partial"] if lock.is_file() else []),
    ]


def build(repo_root: Path, checkout: Path, preset: str) -> None:
    """Compile the checkout into its own `build/<preset>`; only what changed recompiles."""
    ensure_msvc_env()
    args = _build_args(repo_root, checkout, preset)
    run_tool("conan", "build", *args, "--build=missing", *SDK_BUILD_EXCLUSIONS)


def relock(repo_root: Path, preset: str) -> str:
    """Export the editable checkout as a package, pin it in conan.lock, and drop the editable.

    What a commit needs: the plugins build against the cache package CI will resolve, not the
    checkout in place. Returns the package folder for `verify`.
    """
    checkout = editable()
    if checkout is None:
        die("no editable voltmod checkout; register one with `conan editable add <path>`")
    build(repo_root, checkout, preset)
    run_tool("conan", "editable", "remove", str(checkout), check=False)

    # The lock pins the recipe revision only, so an older binary left under the same revision
    # could be picked over the one exported next; drop them first.
    export = run_tool("conan", "export", str(checkout), "--format=json", capture=True)
    ref = json.loads(export.stdout)["reference"]
    run_tool("conan", "remove", f"{ref}:*", "--confirm", check=False)
    args = _build_args(repo_root, checkout, preset)
    exported = run_tool("conan", "export-pkg", *args, "--format=json", capture=True)
    package_id = next(
        node["package_id"]
        for node in json.loads(exported.stdout)["graph"]["nodes"].values()
        if node["ref"].startswith("voltmod/")
    )
    package = run_tool("conan", "cache", "path", f"{ref}:{package_id}", capture=True)

    _pin(repo_root, preset)
    return package.stdout.strip()


def _pin(repo_root: Path, preset: str) -> None:
    """Re-pin `voltmod` in conan.lock to the newest revision in the local cache."""
    lock = repo_root / "conan.lock"
    lock_args: list[str] = []
    if lock.is_file():
        lock_args = [f"--lockfile={lock}"]
        # `--update` never re-pins a revision the lock already names, so the entry is removed first.
        run_tool(
            "conan", "lock", "remove", "--requires=voltmod/*", *lock_args, f"--lockfile-out={lock}"
        )
    profile, settings = host_profile(repo_root, preset)
    run_tool(
        "conan", "lock", "create", str(repo_root),
        "--profile:all", str(profile), *settings, *lock_args,
        f"--lockfile-out={lock}", "--no-remote",
    )


def verify(repo_root: Path, preset: str, package_folder: str) -> None:
    """Die unless the preset's CMake dependency data points at `package_folder`.

    The plugin build tree is kept between framework builds; this is what guarantees it was
    reconfigured against the new package rather than still linking the old one.
    """
    generators = repo_root / "build" / preset / "generators"
    expected = Path(package_folder).resolve().as_posix().lower()
    for data in generators.glob("voltmod-*-data.cmake"):
        if expected in data.read_text(encoding="utf-8").replace("\\", "/").lower():
            return
    die(f"build/{preset} is not configured against {package_folder}; delete it and rebuild")
