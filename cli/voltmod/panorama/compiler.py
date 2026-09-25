import shutil
from dataclasses import dataclass
from pathlib import Path

from voltmod import console
from voltmod.errors import VoltmodError
from voltmod.panorama.sources import panorama_plugins, rendered_dir
from voltmod.platforms import Platform
from voltmod.toolchain.process import run

RESOURCE_COMPILER = f"game/bin/{Platform.WINDOWS.bin_dir}/resourcecompiler.exe"

# Source suffix -> what resourcecompiler writes for it.
COMPILED_SUFFIX = {".xml": ".vxml_c", ".css": ".vcss_c", ".vtex": ".vtex_c"}

# Staged beside the .vtex descriptor that names it; never handed to the compiler.
STAGED_ONLY_SUFFIXES = (".png",)

# The Workshop Manager packs layouts, styles and images only under custom_game.
PANORAMA_DIRS = ("layout/custom_game", "styles/custom_game", "images/custom_game")


@dataclass(frozen=True, slots=True)
class AddonDirs:
    """Where the Workshop Tools read one addon's sources and write its compiled resources."""

    client: Path
    sources: Path
    compiled: Path

    @classmethod
    def of(cls, client: Path, addon: str) -> AddonDirs:
        return cls(
            client, client / "content/csgo_addons" / addon, client / "game/csgo_addons" / addon
        )

    def compiled_path(self, source: Path) -> Path:
        relative = source.relative_to(self.sources)
        return (self.compiled / relative).with_suffix(COMPILED_SUFFIX[source.suffix])


@dataclass(frozen=True, slots=True)
class StagedPlugin:
    name: str
    files: list[Path]

    @property
    def compilable(self) -> list[Path]:
        return [path for path in self.files if path.suffix in COMPILED_SUFFIX]


def stage(root: Path, names: list[str] | None, dirs: AddonDirs) -> list[StagedPlugin]:
    """Copy each named plugin's rendered screens into the addon's sources."""
    staged = []
    for plugin in panorama_plugins(root, names):
        rendered = rendered_dir(root, plugin)
        if files := _stage_files(rendered, dirs.sources):
            staged.append(StagedPlugin(plugin.name, files))
        else:
            console.note(f"{plugin.name}: nothing rendered under {rendered}")
    return staged


def compile_resources(dirs: AddonDirs, staged: list[StagedPlugin]) -> None:
    """Compile every staged source in one resourcecompiler launch."""
    compiler = dirs.client / RESOURCE_COMPILER
    if not compiler.is_file():
        raise VoltmodError(
            f"CS2 Workshop Tools not found at {compiler}\n"
            "Install them from Steam: Library > Tools > Counter-Strike 2 Workshop Tools."
        )

    # The tools only treat a directory with addoninfo.txt as an addon.
    info = dirs.compiled / "addoninfo.txt"
    if not info.is_file():
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text('"AddonInfo"\n{\n}\n', encoding="utf-8")

    compilable = [path for plugin in staged for path in plugin.compilable]
    # One -i per file: wildcards match nothing here, and still report success.
    inputs = [argument for path in compilable for argument in ("-i", path)]
    flags: list[str | Path] = ["-nop4", "-f", "-game", dirs.client / "game/csgo"]
    result = run(compiler, *flags, *inputs, cwd=compiler.parent, capture=True, check=False)

    # It exits 0 whether or not anything compiled, so the expected outputs decide.
    missing = [path for path in compilable if not dirs.compiled_path(path).is_file()]
    if result.returncode != 0 or missing:
        console.info(f"{result.stdout}{result.stderr}".strip())
        if missing:
            names = ", ".join(path.name for path in missing)
            raise VoltmodError(f"resourcecompiler produced no output for: {names}")
        raise VoltmodError(f"resourcecompiler exited {result.returncode}")
    console.note(f"compiled {len(compilable)} resource(s)")


def install_into_client(dirs: AddonDirs, staged: list[StagedPlugin]) -> int:
    """Copy the compiled resources into the client's own csgo/, where a local game loads them."""
    csgo = dirs.client / "game/csgo"
    installed = 0
    for plugin in staged:
        console.section(plugin.name)
        for source in plugin.compilable:
            compiled = dirs.compiled_path(source)
            target = csgo / source.relative_to(dirs.sources).parent / compiled.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(compiled, target)
            console.item(str(target.relative_to(dirs.client)))
            installed += 1
    return installed


def _stage_files(rendered: Path, sources: Path) -> list[Path]:
    """Copy a rendered tree into `sources`, keeping the panorama/ prefix that includes rely on."""
    staged = []
    for source in _rendered_files(rendered):
        target = sources / source.relative_to(rendered.parent)
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
