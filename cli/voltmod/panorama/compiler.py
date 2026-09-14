"""Compiling rendered screens with the CS2 Workshop Tools, then installing them."""

import shutil
import subprocess
from pathlib import Path

from voltmod.cs2_install import RESOURCE_COMPILER, find_client
from voltmod.errors import VoltmodError
from voltmod.panorama.render import rendered_dir, screen_owners
from voltmod.process import WINDOWS

# Source suffix -> what resourcecompiler writes for it.
COMPILED_SUFFIX = {".xml": ".vxml_c", ".css": ".vcss_c", ".vtex": ".vtex_c"}

# Staged beside the .vtex descriptor that names it; never handed to the compiler.
STAGED_ONLY_SUFFIXES = (".png",)

# The only panorama subdirectories gameinfo.gi lets an addon ship custom layouts under.
PANORAMA_DIRS = ("layout/custom_game", "styles/custom_game", "images/custom_game")


def compile_and_install(
    root: Path, names: list[str] | None, client_path: str, addon: str, deploy: bool
) -> None:
    """Compile the named owners' rendered screens, and install them into your client."""
    if not WINDOWS:
        raise VoltmodError("the CS2 Workshop Tools are Windows only; compile the layouts there")

    client = find_client(client_path)
    content = client / "content/csgo_addons" / addon
    built = client / "game/csgo_addons" / addon
    print(f"Client:  {client}")
    print(f"Addon:   csgo_addons/{addon}")

    # One compiler launch for every owner: its startup dominates a run this size.
    staged_by_owner: list[tuple[str, list[Path]]] = []
    for owner in screen_owners(root, names):
        rendered = rendered_dir(root, owner)
        staged = _stage_files(rendered, content)
        if staged:
            staged_by_owner.append((owner.name, staged))
        else:
            print(f"\n--- {owner.name} ---\n  (nothing rendered under {rendered})")

    staged = [path for _, paths in staged_by_owner for path in paths]
    if staged:
        owners = ", ".join(name for name, _ in staged_by_owner)
        print(f"\nStaged {len(staged)} source(s) from {owners}")
        _run_resource_compiler(client, built, staged, content)

    if not deploy:
        print(f"\nCompiled into {built}; not installed.")
        return

    installed = 0
    for name, paths in staged_by_owner:
        print(f"\n--- {name} ---")
        installed += _copy_into_client(client, built, paths, content)
    print(f"\nInstalled {installed} resource(s). Reconnect to pick them up.")


def _stage_files(rendered: Path, content: Path) -> list[Path]:
    """Copy a rendered tree into @p content, keeping the panorama/ prefix that includes rely on."""
    staged = []
    for source in _rendered_files(rendered):
        target = content / source.relative_to(rendered.parent)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        staged.append(target)
    return staged


def _rendered_files(rendered: Path) -> list[Path]:
    # Icon sets nest one level deeper than layouts and styles, so this walks rather than globs.
    return [
        path
        for subdir in PANORAMA_DIRS
        for path in sorted((rendered / subdir).rglob("*"))
        if path.is_file() and path.suffix in (*COMPILED_SUFFIX, *STAGED_ONLY_SUFFIXES)
    ]


def _compilable_files(staged: list[Path]) -> list[Path]:
    return [path for path in staged if path.suffix in COMPILED_SUFFIX]


def _compiled_path(built: Path, staged: Path, content: Path) -> Path:
    return (built / staged.relative_to(content)).with_suffix(COMPILED_SUFFIX[staged.suffix])


def _run_resource_compiler(client: Path, built: Path, staged: list[Path], content: Path) -> None:
    compiler = client / RESOURCE_COMPILER
    if not compiler.is_file():
        raise VoltmodError(
            f"CS2 Workshop Tools not found at {compiler}\n"
            "Install them from Steam: Library > Tools > Counter-Strike 2 Workshop Tools."
        )

    # The tools only treat a directory with addoninfo.txt as an addon.
    info = built / "addoninfo.txt"
    if not info.is_file():
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text('"AddonInfo"\n{\n}\n', encoding="utf-8")

    compilable = _compilable_files(staged)
    command = [str(compiler), "-nop4", "-f", "-game", str(client / "game/csgo")]
    # One -i per file: wildcards match nothing here, and still report success.
    for path in compilable:
        command += ["-i", str(path)]
    result = subprocess.run(command, cwd=compiler.parent, capture_output=True, text=True)

    # It exits 0 whether or not anything compiled, so the expected outputs decide.
    missing = [path for path in compilable if not _compiled_path(built, path, content).is_file()]
    if result.returncode != 0 or missing:
        print(f"{result.stdout}{result.stderr}".strip())
        if missing:
            names = ", ".join(path.name for path in missing)
            raise VoltmodError(f"resourcecompiler produced no output for: {names}")
        raise VoltmodError(f"resourcecompiler exited {result.returncode}")
    print(f"  compiled {len(compilable)} resource(s)")


def _copy_into_client(client: Path, built: Path, staged: list[Path], content: Path) -> int:
    csgo = client / "game/csgo"
    compilable = _compilable_files(staged)
    for source in compilable:
        compiled = _compiled_path(built, source, content)
        target = csgo / source.relative_to(content).parent / compiled.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(compiled, target)
        print(f"  -> {target.relative_to(client)}")
    return len(compilable)
