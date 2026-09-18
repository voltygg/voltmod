"""Conan pieces every build shares: the package remote, host profiles, the editable framework."""

import json
import os
from pathlib import Path
from typing import Any

from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS, run_tool
from voltmod.project import BUNDLED_DIR

REMOTE = "volty"

# Linux CI must consume the published SDK binaries.
SDK_BUILD_EXCLUSIONS = () if WINDOWS else ("--build=!hl2sdk-cs2/*", "--build=!metamod-source/*")


def run_conan_json(*args: str) -> Any:
    return json.loads(run_tool("conan", *args, "--format=json", capture=True).stdout)


def conan_home() -> Path:
    return Path(os.environ.get("CONAN_HOME", Path.home() / ".conan2"))


def profile_dirs(root: Path) -> tuple[Path, ...]:
    """Where Conan profiles are looked for, the project's own first."""
    return (root / "conan/profiles", conan_home() / "profiles")


def profile_args(root: Path, preset: str) -> list[str]:
    """The profile and settings arguments a preset builds with."""
    build_type = "Debug" if "debug" in preset else "Release"
    profiles = next((path for path in profile_dirs(root) if path.is_dir()), None)
    if profiles is None:
        raise VoltmodError(
            "no Conan profiles found; run `voltmod bootstrap` or the setup-toolchain action"
        )
    settings = ["-s", f"build_type={build_type}"]
    if preset.startswith("linux-"):
        return ["--profile:all", str(profiles / "linux-steamrt.txt"), *settings]
    if preset.startswith("windows-"):
        runtime = ["-s", f"compiler.runtime_type={build_type}"]
        return ["--profile:all", str(profiles / "windows-msvc.txt"), *settings, *runtime]
    raise VoltmodError(f"unknown preset: {preset}")


def has_remote() -> bool:
    listing = run_tool("conan", "remote", "list", capture=True, check=False)
    return listing.returncode == 0 and f"{REMOTE}:" in listing.stdout


def remote_url(root: Path) -> str:
    for base in (root, BUNDLED_DIR):
        remotes = base / "conan/remotes.json"
        if remotes.is_file():
            for entry in json.loads(remotes.read_text(encoding="utf-8"))["remotes"]:
                if entry["name"] == REMOTE:
                    return entry["url"]
    raise VoltmodError(f"no '{REMOTE}' remote in any conan/remotes.json")


def ensure_remote(root: Path) -> None:
    """Register the package remote, unless it exists or VOLTMOD_SKIP_REMOTE_SETUP is set."""
    if os.environ.get("VOLTMOD_SKIP_REMOTE_SETUP") or has_remote():
        return
    url = remote_url(root)
    print(f"==> Adding Conan remote '{REMOTE}' ({url})")
    run_tool("conan", "remote", "add", "--force", REMOTE, url)


def find_editable_framework() -> Path | None:
    """The checkout registered with `conan editable add`, read from Conan's registry file.

    Starting Conan to ask costs about a second on every build, so the file is read directly.
    """
    registry = conan_home() / "editable_packages.json"
    if not registry.is_file():
        return None
    try:
        entries = json.loads(registry.read_text(encoding="utf-8"))
        return next(
            (Path(entry["path"]).parent for reference, entry in entries.items()
             if reference.startswith("voltmod/")),
            None,
        )
    except (OSError, ValueError, AttributeError, KeyError, TypeError) as error:
        # Never read an unexpected format as "no editable": the build would link a stale framework.
        raise VoltmodError(f"cannot read {registry}: {error}") from None


def editable_framework(project_root: Path) -> Path | None:
    """The editable checkout the project links, or None when there is none or it is the project."""
    checkout = find_editable_framework()
    if checkout is None or checkout.resolve() == project_root.resolve():
        return None
    return checkout
