"""The project voltmod runs in: its root, .env settings, plugins, and the bundled templates."""

import os
from dataclasses import dataclass, replace
from functools import cache
from pathlib import Path

from dotenv import load_dotenv
from jinja2 import Environment, FileSystemLoader, StrictUndefined, Template, select_autoescape

from voltmod.errors import VoltmodError
from voltmod.process import WINDOWS

_PACKAGE_DIR = Path(__file__).resolve().parent

# A wheel carries templates/ and panorama/ under bundled/; a checkout keeps them at the repo root.
BUNDLED_DIR = (
    _PACKAGE_DIR / "bundled" if (_PACKAGE_DIR / "bundled").is_dir() else _PACKAGE_DIR.parents[1]
)

# `tools/` holds dev-only plugins: installable by name, never by a bare `voltmod install`.
PLUGIN_DIRS = ("plugins", "tools")


def default_preset() -> str:
    return "windows-msvc-release" if WINDOWS else "linux-steamrt-release"


def load_template(name: str) -> Template:
    """One of the bundled Jinja templates, such as `panorama/screen.hpp.j2`."""
    return _template_environment().get_template(name)


@cache
def _template_environment() -> Environment:
    # utf-8-sig: an editor's byte order mark must not reach the output.
    return Environment(
        loader=FileSystemLoader(BUNDLED_DIR / "templates", encoding="utf-8-sig"),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
        autoescape=select_autoescape(["html.j2"]),
    )


@dataclass(frozen=True, slots=True)
class Settings:
    """Local defaults from .env or the environment; a command-line option always wins."""

    server_path: str
    client_path: str
    steamcmd_path: str
    build_preset: str
    map_name: str
    port: int
    max_players: int
    gslt_token: str
    rcon_password: str

    @classmethod
    def from_environment(cls) -> Settings:
        return cls(
            server_path=os.environ.get("CS2_SERVER_PATH", ""),
            client_path=os.environ.get("CS2_CLIENT_PATH", ""),
            steamcmd_path=os.environ.get("STEAMCMD_PATH", ""),
            build_preset=os.environ.get("CS2_BUILD_PRESET", ""),
            map_name=os.environ.get("CS2_MAP") or "de_dust2",
            port=_number_setting("CS2_PORT", 27015),
            max_players=_number_setting("CS2_MAX_PLAYERS", 16),
            gslt_token=os.environ.get("GSLT_TOKEN", ""),
            rcon_password=os.environ.get("RCON_PASSWORD", ""),
        )

    def with_options(self, **options: str | int | None) -> Settings:
        """These settings overridden by each option the command line was given."""
        given = {name: value for name, value in options.items() if value not in (None, "")}
        return replace(self, **given)


@dataclass(frozen=True, slots=True)
class Project:
    root: Path
    settings: Settings

    @classmethod
    def load(cls, root: Path | None = None) -> Project:
        """The project at @p root (default: the working directory), with its .env applied."""
        root = root or Path.cwd()
        # Into os.environ, so conan and cmake child processes see the same values.
        load_dotenv(root / ".env", override=False)
        return cls(root, Settings.from_environment())

    @property
    def lockfile(self) -> Path:
        return self.root / "conan.lock"

    @property
    def cpp_source_dirs(self) -> list[str]:
        """The framework's own source trees, or a consumer's plugins."""
        if (self.root / "include/VoltMod").is_dir():
            return ["src", "include", "tests"]
        return ["plugins"]

    def resolve_preset(self, requested: str | None = None) -> str:
        return requested or self.settings.build_preset or default_preset()

    def build_dir(self, preset: str) -> Path:
        return self.root / "build" / preset

    def plugin_dir(self, name: str) -> Path | None:
        return next(
            (path for parent in PLUGIN_DIRS if (path := self.root / parent / name).is_dir()), None
        )

    def plugin_names(self, requested: str = "") -> list[str]:
        """@p requested, checked to exist, or every plugin with sources under plugins/."""
        if requested:
            if self.plugin_dir(requested) is None:
                searched = " or ".join(f"{parent}/{requested}" for parent in PLUGIN_DIRS)
                raise VoltmodError(f"plugin not found: no {searched}")
            return [requested]

        plugins = self.root / "plugins"
        if not plugins.is_dir():
            raise VoltmodError(f"no plugins directory at {plugins}")
        names = sorted(
            path.name for path in plugins.iterdir() if path.is_dir() and (path / "src").is_dir()
        )
        if not names:
            raise VoltmodError("no plugins found under plugins/")
        return names


def _number_setting(name: str, default: int) -> int:
    value = os.environ.get(name) or str(default)
    try:
        return int(value)
    except ValueError:
        raise VoltmodError(f"{name} must be a number, got '{value}'") from None
