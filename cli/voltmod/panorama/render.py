"""Finding each plugin's Panorama screens and rendering them into the build tree."""

from dataclasses import dataclass
from pathlib import Path

from jinja2 import ChoiceLoader, Environment, FileSystemLoader, StrictUndefined, TemplateError

from voltmod.errors import VoltmodError
from voltmod.files import write_if_changed
from voltmod.panorama.layout import (
    Screen,
    header_namespace,
    member_name,
    pascal_case,
    read_screen,
)
from voltmod.project import BUNDLED_DIR, PLUGIN_DIRS, load_template

BUILD_DIR = "build/panorama"
SCREENS_DIR = "screens"
HEADERS_DIR = "Ui"
IMAGES_DIR = "images/custom_game"
LAYOUT_SUFFIX = ".xml.j2"
STYLESHEET_SUFFIX = ".css.j2"


@dataclass(frozen=True, slots=True)
class ScreenOwner:
    """A plugin that ships a panorama/ tree."""

    name: str
    source: Path


def screen_owners(root: Path, names: list[str] | None = None) -> list[ScreenOwner]:
    """The named plugins that ship a panorama/ tree, or all of them when none is named."""
    found = {
        plugin.name: ScreenOwner(plugin.name, plugin / "panorama")
        for parent in PLUGIN_DIRS
        if (root / parent).is_dir()
        for plugin in (root / parent).iterdir()
        if (plugin / "panorama").is_dir()
    }
    known = sorted(found)
    if not names:
        return [found[name] for name in known]
    unknown = [name for name in names if name not in found]
    if unknown:
        raise VoltmodError(
            f"no panorama sources for {', '.join(unknown)}\nKnown: {', '.join(known) or 'none'}"
        )
    return [found[name] for name in names]


def rendered_dir(root: Path, owner: ScreenOwner, out: Path | None = None) -> Path:
    # Keeps the panorama/ prefix: a layout's `file://{resources}/...` include resolves against it.
    return (out or root / BUILD_DIR) / owner.name / "panorama"


def header_dir(root: Path, owner: ScreenOwner, out: Path | None = None) -> Path:
    """What a plugin puts on its include path for its screen headers."""
    return (out or root / BUILD_DIR) / owner.name / "include"


def screen_sources(owner: ScreenOwner) -> list[Path]:
    return sorted((owner.source / SCREENS_DIR).glob(f"*{LAYOUT_SUFFIX}"))


def screen_name(source: Path) -> str:
    """`hud.xml.j2` -> `hud`."""
    return source.name.removesuffix(LAYOUT_SUFFIX)


def icon_sets(owner: ScreenOwner) -> dict[str, list[str]]:
    """Every icon set the owner ships, as its sorted PNG names; empty sets are skipped."""
    images = owner.source / IMAGES_DIR
    if not images.is_dir():
        return {}
    return {
        directory.name: names
        for directory in sorted(path for path in images.iterdir() if path.is_dir())
        if (names := sorted(png.stem for png in directory.glob("*.png")))
    }


def icon_path(owner: ScreenOwner, icon_set: str, name: str) -> Path:
    """The PNG behind `s2r://panorama/images/custom_game/<set>/<name>.vtex`."""
    return owner.source / IMAGES_DIR / icon_set / f"{name}.png"


def render_screens(
    root: Path, names: list[str] | None = None, out: Path | None = None
) -> list[Path]:
    """Render the named owners' screens, and return the files that changed."""
    written: list[Path] = []
    for owner in screen_owners(root, names):
        written += ScreenRenderer(owner).write_all(root, out)
    return written


def screen_header(screen: Screen, template_source: str) -> str:
    """The C++ header naming @p screen's panels, variables, blocks and class families."""
    return load_template("panorama/screen.hpp.j2").render(
        screen=screen,
        namespace=header_namespace(screen, template_source),
        member_name=member_name,
        pascal_case=pascal_case,
    )


class ScreenRenderer:
    """One owner's screens through one Jinja environment, so the block library compiles once."""

    def __init__(self, owner: ScreenOwner) -> None:
        self.owner = owner
        self.icons = icon_sets(owner)
        # No trimming or escaping: layouts and stylesheets come out exactly as written.
        self.environment = Environment(
            loader=ChoiceLoader(
                [
                    FileSystemLoader(owner.source / SCREENS_DIR, encoding="utf-8-sig"),
                    FileSystemLoader(BUNDLED_DIR / "panorama/blocks", encoding="utf-8-sig"),
                ]
            ),
            undefined=StrictUndefined,
            keep_trailing_newline=True,
            autoescape=False,
        )
        # Globals, because a block reached through `{% import %}` cannot see the caller's variables.
        self.environment.globals["images"] = self.icons

    def render(self, name: str) -> tuple[str, str]:
        self.environment.globals["screen"] = name
        return (
            self._render_template(f"{name}{LAYOUT_SUFFIX}"),
            self._render_template(f"{name}{STYLESHEET_SUFFIX}"),
        )

    def write_all(self, root: Path, out: Path | None) -> list[Path]:
        """Render every screen, header and icon into the build tree; return what changed."""
        target = rendered_dir(root, self.owner, out)
        headers = header_dir(root, self.owner, out) / HEADERS_DIR

        written: list[Path] = []
        for source in screen_sources(self.owner):
            name = screen_name(source)
            layout, stylesheet = self.render(name)
            written += _write(target / "layout/custom_game" / f"{name}.xml", layout)
            written += _write(target / "styles/custom_game" / f"{name}.css", stylesheet)
            screen = read_screen(layout, stylesheet, source)
            header = screen_header(screen, source.read_text(encoding="utf-8-sig"))
            written += _write(headers / f"{pascal_case(name)}.hpp", header)
        return written + self._write_icons(target)

    def _render_template(self, template: str) -> str:
        try:
            return self.environment.get_template(template).render()
        except TemplateError as error:
            raise VoltmodError(f"{self.owner.source / SCREENS_DIR / template}: {error}") from None

    def _write_icons(self, target: Path) -> list[Path]:
        # resourcecompiler compiles the .vtex descriptor, which names the PNG beside it.
        descriptor = load_template("panorama/icon.vtex.j2")
        written: list[Path] = []
        for icon_set, names in self.icons.items():
            for name in names:
                png = target / IMAGES_DIR / icon_set / f"{name}.png"
                source = f"panorama/{IMAGES_DIR}/{icon_set}/{name}.png"
                written += _write(png, icon_path(self.owner, icon_set, name).read_bytes())
                written += _write(png.with_suffix(".vtex"), descriptor.render(source=source))
        return written


def _write(path: Path, data: str | bytes) -> list[Path]:
    return [path] if write_if_changed(path, data) else []
