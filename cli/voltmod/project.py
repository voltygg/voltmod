import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from dotenv import load_dotenv

from voltmod.errors import VoltmodError
from voltmod.framework.paths import is_framework
from voltmod.toolchain.process import WINDOWS

# `tools/` holds dev-only plugins: installable by name, never by a bare `voltmod install`.
PLUGIN_DIRS = ("plugins", "tools")


def default_preset() -> str:
    return "windows-msvc-release" if WINDOWS else "linux-steamrt-release"


@dataclass(frozen=True, slots=True)
class PluginDatabase:
    """The `database` block of plugin.json: where `voltmod database header` reads and writes."""

    migrations: Path
    header: Path
    namespace: str


@dataclass(frozen=True, slots=True)
class Plugin:
    name: str
    dir: Path

    @property
    def manifest_path(self) -> Path:
        return self.dir / "plugin.json"

    @property
    def panorama_dir(self) -> Path:
        """Screens, icons and templates the plugin ships for the Panorama UI."""
        return self.dir / "panorama"

    def manifest(self) -> dict[str, Any]:
        if not self.manifest_path.is_file():
            return {}
        try:
            return json.loads(self.manifest_path.read_text(encoding="utf-8"))
        except ValueError as error:
            raise VoltmodError(f"{self.manifest_path}: {error}") from None

    @property
    def database(self) -> PluginDatabase | None:
        entry = self.manifest().get("database")
        if entry is None:
            return None
        try:
            return PluginDatabase(
                self.dir / entry["migrations"], self.dir / entry["header"], entry["namespace"]
            )
        except KeyError, TypeError:
            raise VoltmodError(
                f"{self.manifest_path}: database needs migrations, header and namespace"
            ) from None


@dataclass(frozen=True, slots=True)
class Project:
    root: Path

    @classmethod
    def load(cls, root: Path | None = None) -> Project:
        """The project at `root` (default: the working directory), with its .env applied."""
        root = root or Path.cwd()
        # Into os.environ, so conan and cmake child processes see the same values.
        load_dotenv(root / ".env", override=False)
        return cls(root)

    @property
    def lockfile(self) -> Path:
        return self.root / "conan.lock"

    @property
    def is_framework(self) -> bool:
        """Whether this is the VoltMod checkout itself rather than a consumer of it."""
        return is_framework(self.root)

    @property
    def cpp_source_dirs(self) -> list[str]:
        """The framework's own source trees, or a consumer's plugins."""
        return ["src", "include", "tests"] if self.is_framework else ["plugins"]

    def build_dir(self, preset: str) -> Path:
        return self.root / "build" / preset

    def plugins(self, *, include_tools: bool = False) -> list[Plugin]:
        """Every plugin directory under plugins/, and tools/ when asked, sorted by name."""
        parents = PLUGIN_DIRS if include_tools else PLUGIN_DIRS[:1]
        found = [
            Plugin(path.name, path)
            for parent in parents
            if (self.root / parent).is_dir()
            for path in (self.root / parent).iterdir()
            if path.is_dir()
        ]
        return sorted(found, key=lambda plugin: plugin.name)

    def plugin(self, name: str) -> Plugin:
        for parent in PLUGIN_DIRS:
            if (path := self.root / parent / name).is_dir():
                return Plugin(name, path)
        searched = " or ".join(f"{parent}/{name}" for parent in PLUGIN_DIRS)
        raise VoltmodError(f"plugin not found: no {searched}")

    def installable_plugins(self, names: list[str]) -> list[Plugin]:
        """The named plugins, or every plugins/ entry that has a plugin.json."""
        if names:
            return [self.plugin(name) for name in names]
        found = [plugin for plugin in self.plugins() if plugin.manifest_path.is_file()]
        if not found:
            raise VoltmodError(f"no plugins with a plugin.json under {self.root / 'plugins'}")
        return found
