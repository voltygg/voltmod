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

from voltmod.tools import WINDOWS, abort

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


def _is_client(root: Path) -> bool:
    return (root / "game/csgo/gameinfo.gi").is_file()


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
        if not _is_client(root):
            abort(f"no CS2 client at {root}\nExpected {root / 'game/csgo/gameinfo.gi'}")
        return root

    for candidate in _STEAM_ROOTS:
        steam = Path(candidate).expanduser()
        if steam.is_dir():
            for library in _library_paths(steam):
                if _is_client(library / _CS2_IN_LIBRARY):
                    return library / _CS2_IN_LIBRARY

    abort("no CS2 client found; set CS2_CLIENT_PATH in .env or pass --client-path")


def _rendered_files(rendered: Path) -> list[Path]:
    """What one rendered tree holds, in the whitelisted subdirectories.

    Icon sets nest one directory deeper than layouts and styles, so this walks rather than globs.
    """
    return [
        path
        for subdir in PANORAMA_DIRS
        for path in sorted((rendered / subdir).rglob("*"))
        if path.is_file() and path.suffix in (*COMPILED_SUFFIX, *STAGED_ONLY)
    ]


def _stage(rendered: Path, content: Path) -> list[Path]:
    """Copy a rendered tree into @p content, keeping the `panorama/` prefix.

    The prefix is load-bearing, not cosmetic: a layout's `<include src="file://{resources}/...">`
    resolves against the addon root, so a stylesheet staged without it is reported missing.
    """
    staged = []
    for source in _rendered_files(rendered):
        target = content / source.relative_to(rendered.parent)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        staged.append(target)
    return staged


def _compilable(staged: list[Path]) -> list[Path]:
    return [path for path in staged if path.suffix in COMPILED_SUFFIX]


def _compiled_path(built: Path, staged: Path, content: Path) -> Path:
    """Where resourcecompiler writes the artifact for a staged source."""
    return (built / staged.relative_to(content)).with_suffix(COMPILED_SUFFIX[staged.suffix])


def _compile(client: Path, built: Path, staged: list[Path], content: Path) -> None:
    """Run resourcecompiler over the staged sources.

    Files are passed one `-i` at a time rather than as a wildcard: the compiler documents
    wildcards but matches nothing for these, and reports that as a successful run over zero files.
    """
    compiler = client / _COMPILER
    if not compiler.is_file():
        abort(
            f"CS2 Workshop Tools not found at {compiler}\n"
            "Install them from Steam: Library > Tools > Counter-Strike 2 Workshop Tools."
        )

    # The tools do not treat a directory without addoninfo.txt as an addon.
    info = built / "addoninfo.txt"
    if not info.is_file():
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text('"AddonInfo"\n{\n}\n', encoding="utf-8")

    compilable = _compilable(staged)
    command = [str(compiler), "-nop4", "-f", "-game", str(client / "game/csgo")]
    for path in compilable:
        command += ["-i", str(path)]

    result = subprocess.run(command, cwd=compiler.parent, capture_output=True, text=True)

    # It exits 0 whether or not anything compiled, and its console tally is prose that any tools
    # update may reword, so the artifacts it was asked to produce are what decide.
    missing = [path for path in compilable if not _compiled_path(built, path, content).is_file()]
    if result.returncode != 0 or missing:
        print(f"{result.stdout}{result.stderr}".strip())
        if missing:
            abort("resourcecompiler produced no output for: " + ", ".join(p.name for p in missing))
        abort(f"resourcecompiler exited {result.returncode}")

    print(f"  compiled {len(compilable)} resource(s)")


def _deploy(client: Path, built: Path, staged: list[Path], content: Path) -> int:
    """Copy the compiled resources into the client's own csgo tree."""
    csgo = client / "game/csgo"
    compilable = _compilable(staged)
    for source in compilable:
        compiled = _compiled_path(built, source, content)
        target = csgo / source.relative_to(content).parent / compiled.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(compiled, target)
        print(f"  -> {target.relative_to(client)}")
    return len(compilable)


def install(root: Path, names: list[str], client_path: str, addon: str, deploy: bool) -> None:
    """Compile the named owners' rendered screens and install them into the client."""
    if not WINDOWS:
        abort("the CS2 Workshop Tools are Windows only; compile the layouts there")

    client = find_client(client_path)
    content = client / "content/csgo_addons" / addon
    built = client / "game/csgo_addons" / addon

    print(f"Client:  {client}")
    print(f"Addon:   csgo_addons/{addon}")

    # Every owner stages into the same addon tree, so they compile in one launch: resourcecompiler
    # startup dominates the run for this many files, and paying it per owner adds nothing.
    by_owner: list[tuple[str, list[Path]]] = []
    for owner in select(find_owners(root), names).values():
        rendered = output(root, owner)
        staged = _stage(rendered, content)
        if staged:
            by_owner.append((owner.name, staged))
        else:
            print(f"\n--- {owner.name} ---\n  (nothing rendered under {rendered})")

    staged = [path for _, paths in by_owner for path in paths]
    if staged:
        print(f"\nStaged {len(staged)} source(s) from {', '.join(name for name, _ in by_owner)}")
        _compile(client, built, staged, content)

    if not deploy:
        print(f"\nCompiled into {built}; not installed.")
        return

    deployed = 0
    for name, paths in by_owner:
        print(f"\n--- {name} ---")
        deployed += _deploy(client, built, paths, content)
    print(f"\nInstalled {deployed} resource(s). Reconnect to pick them up.")


def publish(root: Path, names: list[str], directory: Path) -> int:
    """Copy the named owners' rendered trees into @p directory, `panorama/` prefix intact.

    No client and no compiler: this is what a workshop addon's content directory wants.
    """
    owners = select(find_owners(root), names).values()
    return sum(len(_stage(output(root, owner), directory)) for owner in owners)
