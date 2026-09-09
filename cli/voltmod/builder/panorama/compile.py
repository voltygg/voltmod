"""Compile rendered Panorama screens with the CS2 Workshop Tools and drop them into a client.

A client renders a layout only if it already has the *compiled* resource on disk, so a screen
shows nothing until `resourcecompiler.exe` has run over its `.xml` and `.css`. This is the local
half of that: compile, then install into your own client, so a `meta reload` and a reconnect is
the whole iteration.

Getting a layout to *other* players is a workshop addon instead: `publish` writes the tree the
Workshop Tools expect.
"""

import re
import shutil
import subprocess
from pathlib import Path

from voltmod.tools import WINDOWS, die

from .screens import find_owners, output, select

#: Source extension -> what resourcecompiler writes for it.
COMPILED_SUFFIX = {".xml": ".vxml_c", ".css": ".vcss_c", ".vtex": ".vtex_c"}

#: Staged beside its .vtex descriptor, which is what names it; the compiler is never given one.
STAGED_ONLY = (".png",)

#: The only panorama subdirectories gameinfo.gi's addon whitelist allows a custom layout under.
PANORAMA_DIRS = ("layout/custom_game", "styles/custom_game", "images/custom_game")

#: Where the compiler sits inside a client installation.
_COMPILER = "game/bin/win64/resourcecompiler.exe"

#: Tried in order when neither --client-path nor CS2_CLIENT_PATH says where the client is.
_STEAM_ROOTS = (
    "C:/Program Files (x86)/Steam",
    "C:/Program Files/Steam",
    "~/.steam/steam",
    "~/.local/share/Steam",
)

_CS2_IN_LIBRARY = "steamapps/common/Counter-Strike Global Offensive"

_LIBRARY_PATH_RE = re.compile(r'"path"\s+"([^"]+)"')


def _library_paths(steam: Path) -> list[Path]:
    """Every Steam library on this machine, so a client on a second drive is still found."""
    libraries = [steam]
    manifest = steam / "steamapps/libraryfolders.vdf"
    if manifest.is_file():
        text = manifest.read_text(encoding="utf-8", errors="replace")
        libraries += [Path(found.replace("\\\\", "/")) for found in _LIBRARY_PATH_RE.findall(text)]
    return libraries


def find_client(client_path: str) -> Path:
    """Locate a CS2 client installation, or say how to name one."""
    if client_path:
        root = Path(client_path).expanduser()
        if not (root / "game/csgo/gameinfo.gi").is_file():
            die(f"no CS2 client at {root}\nExpected {root / 'game/csgo/gameinfo.gi'}")
        return root

    for candidate in _STEAM_ROOTS:
        steam = Path(candidate).expanduser()
        if not steam.is_dir():
            continue
        for library in _library_paths(steam):
            root = library / _CS2_IN_LIBRARY
            if (root / "game/csgo/gameinfo.gi").is_file():
                return root

    die("no CS2 client found; set CS2_CLIENT_PATH in .env or pass --client-path")


def _rendered_files(rendered: Path) -> list[Path]:
    """What one rendered tree holds, in the whitelisted subdirectories.

    Icon sets nest one directory deeper than layouts and styles, so this walks rather than globs.
    """
    files: list[Path] = []
    for subdir in PANORAMA_DIRS:
        for path in sorted((rendered / subdir).rglob("*")):
            if path.is_file() and path.suffix in (*COMPILED_SUFFIX, *STAGED_ONLY):
                files.append(path)
    return files


def _stage(sources: list[Path], rendered: Path, content: Path) -> list[Path]:
    """Copy sources into the addon's content tree, keeping the `panorama/` prefix.

    The prefix is load-bearing, not cosmetic: a layout's `<include src="file://{resources}/...">`
    resolves against the addon root, so a stylesheet staged without it is reported missing.
    """
    staged = []
    for source in sources:
        target = content / source.relative_to(rendered.parent)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        staged.append(target)
    return staged


def _compiled_path(built: Path, staged: Path, content: Path) -> Path:
    """Where resourcecompiler writes the artifact for a staged source."""
    return (built / staged.relative_to(content)).with_suffix(COMPILED_SUFFIX[staged.suffix])


def _compile(client: Path, addon: str, staged: list[Path], content: Path) -> None:
    """Run resourcecompiler over the staged sources.

    Files are passed one `-i` at a time rather than as a wildcard: the compiler documents
    wildcards but matches nothing for these, and reports that as a successful run over zero files.
    """
    compiler = client / _COMPILER
    if not compiler.is_file():
        die(
            f"CS2 Workshop Tools not found at {compiler}\n"
            "Install them from Steam: Library > Tools > Counter-Strike 2 Workshop Tools."
        )

    # The tools do not treat a directory without addoninfo.txt as an addon.
    info = client / "game/csgo_addons" / addon / "addoninfo.txt"
    if not info.is_file():
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text('"AddonInfo"\n{\n}\n', encoding="utf-8")

    compilable = [path for path in staged if path.suffix in COMPILED_SUFFIX]
    command = [str(compiler), "-nop4", "-f", "-game", str(client / "game/csgo")]
    for path in compilable:
        command += ["-i", str(path)]

    result = subprocess.run(command, cwd=compiler.parent, capture_output=True, text=True)
    output_text = f"{result.stdout}{result.stderr}"

    # It exits 0 whether or not anything compiled, and its console tally is prose that any tools
    # update may reword, so the artifacts it was asked to produce are what decide.
    built = client / "game/csgo_addons" / addon
    missing = [path for path in compilable if not _compiled_path(built, path, content).is_file()]
    if result.returncode != 0 or missing:
        print(output_text.strip())
        if missing:
            die("resourcecompiler produced no output for: " + ", ".join(p.name for p in missing))
        die(f"resourcecompiler exited {result.returncode}")

    print(f"  compiled {len(compilable)} resource(s)")


def _deploy(client: Path, addon: str, staged: list[Path], content: Path) -> int:
    """Copy the compiled resources into the client's own csgo tree."""
    built = client / "game/csgo_addons" / addon
    csgo = client / "game/csgo"
    count = 0

    for source in staged:
        if source.suffix not in COMPILED_SUFFIX:
            continue
        relative = source.relative_to(content)
        compiled = _compiled_path(built, source, content)
        target = csgo / relative.parent / compiled.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(compiled, target)
        print(f"  -> {target.relative_to(client)}")
        count += 1

    return count


def install(root: Path, names: list[str], client_path: str, addon: str, deploy: bool) -> None:
    """Compile the named owners' rendered screens and install them into the client."""
    if not WINDOWS:
        die("the CS2 Workshop Tools are Windows only; compile the layouts there")

    client = find_client(client_path)
    owners = select(find_owners(root), names)

    print(f"Client:  {client}")
    print(f"Addon:   csgo_addons/{addon}")

    content = client / "content/csgo_addons" / addon
    deployed = 0

    # Every owner stages into the same addon tree, so they compile in one launch: resourcecompiler
    # startup dominates the run for this many files, and paying it per owner adds nothing.
    by_owner: list[tuple[str, list[Path]]] = []
    for owner in owners.values():
        rendered = output(root, owner)
        sources = _rendered_files(rendered)
        if not sources:
            print(f"\n--- {owner.name} ---\n  (nothing rendered under {rendered})")
            continue
        by_owner.append((owner.name, _stage(sources, rendered, content)))

    staged = [path for _, paths in by_owner for path in paths]
    if staged:
        print(f"\nStaged {len(staged)} source(s) from {', '.join(name for name, _ in by_owner)}")
        _compile(client, addon, staged, content)

    if deploy:
        for name, paths in by_owner:
            print(f"\n--- {name} ---")
            deployed += _deploy(client, addon, paths, content)
        print(f"\nInstalled {deployed} resource(s). Reconnect to pick them up.")
    else:
        print(f"\nCompiled into {client / 'game/csgo_addons' / addon}; not installed.")


def publish(root: Path, names: list[str], directory: Path) -> int:
    """Copy the named owners' rendered trees into @p directory, `panorama/` prefix intact.

    No client and no compiler: this is what a workshop addon's content directory wants.
    """
    owners = select(find_owners(root), names)
    count = 0

    for owner in owners.values():
        rendered = output(root, owner)
        for source in _rendered_files(rendered):
            target = directory / source.relative_to(rendered.parent)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            count += 1

    return count
