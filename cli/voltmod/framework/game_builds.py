"""Archiving each CS2 build's server binaries, so an update can be diffed against the last one.

Steam serves only the current build to an anonymous login; a build not archived on its day is gone.
"""

import hashlib
import io
import json
import os
import platform as host
import re
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.framework.gamedata import game_libraries
from voltmod.platforms import Platform
from voltmod.server.cs2_server import CSGO_DIR, STEAM_INF, Cs2Server
from voltmod.server.install import HOST_GAMEDATA
from voltmod.steam import CS2_APP
from voltmod.toolchain.process import WINDOWS, run

DEPOT_DOWNLOADER_VERSION = "3.4.0"
# Release asset and SHA-256 per host; a changed hash means a changed binary, so refuse it.
DEPOT_DOWNLOADER_ASSETS = {
    ("Windows", "AMD64"): (
        "DepotDownloader-windows-x64.zip",
        "41c9e9f0df54b3ad02e67a11726756e5c73283bd7c2e1b04acfa5ae4c2ed3767",
    ),
    ("Linux", "x86_64"): (
        "DepotDownloader-linux-x64.zip",
        "a999dec66b4850fc961bd50366696d23c2d0fad7b18790e6a5647b2f19097a53",
    ),
}


def default_archive() -> Path:
    return Path(os.environ.get("CS2_BUILD_ARCHIVE") or "~/.voltmod/cs2-builds").expanduser()


def fetch_build(archive: Path, platform: Platform) -> Path:
    """Download the current build's gamedata binaries into `archive/<build>/<platform>`."""
    files = [*game_libraries(platform).values(), STEAM_INF]
    archive.mkdir(parents=True, exist_ok=True)
    downloader = _depot_downloader(archive / "tools")
    staging = Path(tempfile.mkdtemp(prefix=f"fetch-{platform}-", dir=archive))
    try:
        file_list = staging / "files.txt"
        file_list.write_text(
            "".join(f"regex:^{re.escape(path)}$\n" for path in files), encoding="utf-8"
        )
        install = staging / "install"
        app = ["-app", CS2_APP, "-os", platform, "-osarch", "64"]
        output: list[str | Path] = ["-filelist", file_list, "-dir", install]
        result = run(downloader, *app, *output, capture=True, check=False)
        missing = [path for path in files if not (install / path).is_file()]
        if result.returncode or missing:
            raise VoltmodError(
                f"DepotDownloader did not fetch {', '.join(missing) or 'the build'}:\n"
                f"{result.stdout[-2000:]}{result.stderr[-2000:]}"
            )

        target = archive / Cs2Server(install).build / platform
        if target.is_dir():
            console.note(f"build {target.parent.name} {platform} is already archived")
            return target
        target.mkdir(parents=True)
        for path in files:
            (target / path).parent.mkdir(parents=True, exist_ok=True)
            shutil.move(install / path, target / path)
        return target
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def resolved_record(build_dir: Path, platform: Platform) -> Path:
    return build_dir / f"resolved.{platform}.json"


def archive_resolved(server: Path, archive: Path, platform: Platform) -> str | None:
    """File the server's resolved record under the build it names, if that build is archived.

    On update day the server still holds the old build's record: the addresses to diff against.
    """
    record = resolved_record(server / CSGO_DIR / Path(HOST_GAMEDATA).parent, platform)
    if not record.is_file():
        return None
    build = str(json.loads(record.read_text(encoding="utf-8")).get("build"))
    target = archive / build / platform
    if not target.is_dir():
        return None
    shutil.copy2(record, resolved_record(target, platform))
    return build


def archived_builds(archive: Path) -> list[str]:
    """Archived build numbers, oldest first."""
    if not archive.is_dir():
        return []
    return sorted((entry.name for entry in archive.iterdir() if entry.name.isdigit()), key=int)


def _depot_downloader(tools: Path) -> Path:
    """The pinned DepotDownloader, downloaded and hash-checked on first use."""
    asset = DEPOT_DOWNLOADER_ASSETS.get((host.system(), host.machine()))
    if asset is None:
        raise VoltmodError(f"no pinned DepotDownloader for {host.system()} {host.machine()}")
    name, digest = asset
    folder = tools / f"DepotDownloader-{DEPOT_DOWNLOADER_VERSION}"
    executable = folder / ("DepotDownloader.exe" if WINDOWS else "DepotDownloader")
    if executable.is_file():
        return executable

    url = (
        "https://github.com/SteamRE/DepotDownloader/releases/download/"
        f"DepotDownloader_{DEPOT_DOWNLOADER_VERSION}/{name}"
    )
    console.note(f"downloading {name}")
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise VoltmodError(f"{name} does not match its pinned SHA-256; refusing to run it")
    with zipfile.ZipFile(io.BytesIO(data)) as bundle:
        bundle.extractall(folder)
    executable.chmod(0o755)
    return executable
