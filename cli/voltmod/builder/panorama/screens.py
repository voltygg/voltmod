"""Find a plugin's Panorama screens and render them into the build tree.

One screen is `panorama/screens/<name>.xml.j2` plus `panorama/screens/<name>.css`, Jinja over the
framework's block library. Rendering writes a layout, a stylesheet, the icons a screen references,
and the C++ header naming its parts. Nothing rendered is committed.

The framework owns no screens. It ships `panorama/blocks/`, which templates reach through the
Jinja loader rather than by owning a tree.
"""

from dataclasses import dataclass
from pathlib import Path
from xml.etree import ElementTree

from jinja2 import ChoiceLoader, Environment, FileSystemLoader, StrictUndefined, TemplateError

from voltmod.localdev import PLUGIN_DIRS
from voltmod.tools import abort

from . import bind

#: Rendered trees live here, one directory per owner, under the project's build tree.
BUILD_DIR = "build/panorama"

SCREENS_DIR = "screens"
HEADERS_DIR = "Ui"
BLOCKS_DIR = "panorama/blocks"
IMAGES_DIR = "images/custom_game"

SUFFIX = ".xml.j2"

#: resourcecompiler compiles this descriptor, never the PNG, so both go into the rendered tree.
VTEX_TEMPLATE = "panorama/icon.vtex.in"


@dataclass(frozen=True, slots=True)
class Owner:
    """A plugin that ships a `panorama/` tree."""

    name: str
    source: Path


def find_owners(root: Path) -> dict[str, Owner]:
    """Every plugin `panorama/` tree this project renders, keyed by plugin name."""
    found = {
        plugin.name: Owner(plugin.name, plugin / "panorama")
        for parent in PLUGIN_DIRS
        if (root / parent).is_dir()
        for plugin in sorted((root / parent).iterdir())
        if (plugin / "panorama").is_dir()
    }
    return dict(sorted(found.items()))


def select(owners: dict[str, Owner], names: list[str]) -> dict[str, Owner]:
    """The named owners, or all of them when nothing is named."""
    if not names:
        return owners
    unknown = [name for name in names if name not in owners]
    if unknown:
        abort(f"no panorama sources for {', '.join(unknown)}\nKnown: {', '.join(owners) or 'none'}")
    return {name: owners[name] for name in names}


def output(root: Path, owner: Owner, out: Path | None = None) -> Path:
    """Where @p owner's rendered tree goes.

    The `panorama/` prefix is load-bearing: a layout's `<include src="file://{resources}/...">`
    resolves against it, so it survives into the addon and into a publish directory.
    """
    return (out or root / BUILD_DIR) / owner.name / "panorama"


def includes(root: Path, owner: Owner, out: Path | None = None) -> Path:
    """Where @p owner's screen headers go: what a plugin puts on its include path."""
    return (out or root / BUILD_DIR) / owner.name / "include"


def sources(owner: Owner) -> list[Path]:
    """Every screen template @p owner ships."""
    return sorted((owner.source / SCREENS_DIR).glob(f"*{SUFFIX}"))


def stem(source: Path) -> str:
    """The screen name a template file carries: `hud.xml.j2` -> `hud`."""
    return source.name.removesuffix(SUFFIX)


def icon_sets(owner: Owner) -> dict[str, list[str]]:
    """Every icon set the owner ships, each as its sorted PNG names; empty sets are skipped."""
    root = owner.source / IMAGES_DIR
    if not root.is_dir():
        return {}
    return {
        directory.name: pngs
        for directory in sorted(path for path in root.iterdir() if path.is_dir())
        if (pngs := sorted(png.stem for png in directory.glob("*.png")))
    }


def icon(owner: Owner, icon_set: str, name: str) -> Path:
    """The PNG behind `s2r://panorama/images/custom_game/<set>/<name>.vtex`."""
    return owner.source / IMAGES_DIR / icon_set / f"{name}.png"


def render(
    root: Path, framework_root: Path, names: list[str], out: Path | None = None
) -> list[Path]:
    """Render the named owners' screens, and return what was written."""
    written: list[Path] = []
    for owner in select(find_owners(root), names).values():
        written += Renderer(owner, framework_root).write_all(root, out)
    return written


def screen(owner: Owner, framework_root: Path, name: str) -> tuple[str, str]:
    """One screen rendered in memory: its layout and its stylesheet."""
    return Renderer(owner, framework_root).screen(name)


def write(path: Path, data: bytes | str) -> list[Path]:
    """Write @p data unless it is already there, so a second run touches nothing."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    if path.is_file() and path.read_bytes() == data:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return [path]


class Renderer:
    """One owner's screens through one Jinja environment.

    Every screen shares the block library, and re-reading it per screen re-compiles every block,
    so anything rendering more than one screen of an owner should hold one of these.
    """

    def __init__(self, owner: Owner, framework_root: Path) -> None:
        self.owner = owner
        self.framework_root = framework_root
        self.icons = icon_sets(owner)
        # utf-8-sig: an editor's byte order mark would otherwise reach the client as a parse error.
        self.environment = Environment(
            loader=ChoiceLoader(
                [
                    FileSystemLoader(owner.source / SCREENS_DIR, encoding="utf-8-sig"),
                    FileSystemLoader(framework_root / BLOCKS_DIR, encoding="utf-8-sig"),
                ]
            ),
            undefined=StrictUndefined,
            keep_trailing_newline=True,
            autoescape=False,
        )
        # Globals rather than render arguments: a block reached through `{% import %}` builds ids
        # from the screen name and icon sets too, and an import does not see the caller's locals.
        self.environment.globals["images"] = self.icons

    def screen(self, name: str) -> tuple[str, str]:
        """@p name rendered in memory: its layout and its stylesheet."""
        self.environment.globals["screen"] = name
        return self._render(f"{name}{SUFFIX}"), self._render(f"{name}.css")

    def write_all(self, root: Path, out: Path | None) -> list[Path]:
        """Render every screen and icon into the build tree, and return what changed."""
        target = output(root, self.owner, out)
        headers = includes(root, self.owner, out) / HEADERS_DIR

        written: list[Path] = []
        for source in sources(self.owner):
            name = stem(source)
            layout, stylesheet = self.screen(name)
            written += write(target / "layout/custom_game" / f"{name}.xml", layout)
            written += write(target / "styles/custom_game" / f"{name}.css", stylesheet)
            header = _header(source, layout, stylesheet)
            written += write(headers / f"{bind.pascal(name)}.hpp", header)
        return written + self._icons(target)

    def _render(self, template: str) -> str:
        try:
            return self.environment.get_template(template).render()
        except TemplateError as error:
            abort(f"{self.owner.source / SCREENS_DIR / template}: {error}")

    def _icons(self, target: Path) -> list[Path]:
        """Copy each icon into the rendered tree beside the descriptor that names it."""
        if not self.icons:
            return []
        descriptor = (self.framework_root / VTEX_TEMPLATE).read_text(encoding="utf-8")
        written: list[Path] = []
        for icon_set, names in self.icons.items():
            for name in names:
                out = target / IMAGES_DIR / icon_set / f"{name}.png"
                source = f"panorama/{IMAGES_DIR}/{icon_set}/{name}.png"
                written += write(out, icon(self.owner, icon_set, name).read_bytes())
                written += write(out.with_suffix(".vtex"), descriptor.replace("%SOURCE%", source))
        return written


def _header(source: Path, layout: str, stylesheet: str) -> str:
    """The screen's C++ header text; the template is read for its namespace directive."""
    try:
        parsed = bind.read(layout, stylesheet)
    except ElementTree.ParseError as error:
        abort(f"{source}: the rendered layout is not well-formed XML: {error}")
    return bind.header(parsed, source.read_text(encoding="utf-8-sig"))
