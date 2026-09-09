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
from voltmod.tools import die

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
        die(f"no panorama sources for {', '.join(unknown)}\nKnown: {', '.join(owners) or 'none'}")
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


def render(root: Path, kit_root: Path, names: list[str], out: Path | None = None) -> list[Path]:
    """Render the named owners' screens, and return what was written."""
    written: list[Path] = []
    for owner in select(find_owners(root), names).values():
        written += _owner(owner, root, kit_root, out)
    return written


def screen(owner: Owner, kit_root: Path, name: str) -> tuple[str, str]:
    """One screen rendered in memory: its layout and its stylesheet."""
    return _screen(_environment(owner, kit_root, _icon_sets(owner)), owner, name)


def put(path: Path, text: str) -> list[Path]:
    """Write @p text unless it is already there, so a second run touches nothing."""
    return write(path, text.encode("utf-8"))


def write(path: Path, data: bytes) -> list[Path]:
    if path.is_file() and path.read_bytes() == data:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return [path]


def _owner(owner: Owner, root: Path, kit_root: Path, out: Path | None) -> list[Path]:
    target = output(root, owner, out)

    # One environment and one icon scan for the whole owner: every screen shares the block
    # library, and re-reading it per screen re-compiles every block.
    icons = _icon_sets(owner)
    environment = _environment(owner, kit_root, icons)

    written: list[Path] = []
    for source in sources(owner):
        name = source.name.removesuffix(SUFFIX)
        layout, stylesheet = _screen(environment, owner, name)

        written += put(target / "layout/custom_game" / f"{name}.xml", layout)
        written += put(target / "styles/custom_game" / f"{name}.css", stylesheet)
        written += _header(owner, root, out, source, layout, stylesheet)

    return written + _icons(owner, kit_root, target, icons)


def _header(
    owner: Owner, root: Path, out: Path | None, source: Path, layout: str, stylesheet: str
) -> list[Path]:
    """The screen's C++ header, beside the rendered tree rather than in it."""
    try:
        parsed = bind.read(layout, stylesheet)
    except ElementTree.ParseError as error:
        die(f"{source}: the rendered layout is not well-formed XML: {error}")

    name = bind.pascal(source.name.removesuffix(SUFFIX))
    text = bind.header(parsed, source.read_text(encoding="utf-8-sig"))
    return put(includes(root, owner, out) / HEADERS_DIR / f"{name}.hpp", text)


def _screen(environment: Environment, owner: Owner, name: str) -> tuple[str, str]:
    # Global rather than a render argument: a block reached through `{% import %}` builds ids
    # from the screen name too, and an import does not see the caller's locals.
    environment.globals["screen"] = name
    layout = owner.source / SCREENS_DIR / f"{name}{SUFFIX}"
    style = owner.source / SCREENS_DIR / f"{name}.css"
    return _render(environment, layout), _render(environment, style)


def _environment(owner: Owner, kit_root: Path, images: dict[str, list[str]]) -> Environment:
    """Templates resolve against the owner's screens first, then the framework's block library.

    The icon sets and the screen name are globals rather than render arguments so that a block
    reached through `{% import %}` sees them too.
    """
    # utf-8-sig: an editor's byte order mark would otherwise reach the client as a parse error.
    environment = Environment(
        loader=ChoiceLoader(
            [
                FileSystemLoader(owner.source / SCREENS_DIR, encoding="utf-8-sig"),
                FileSystemLoader(kit_root / BLOCKS_DIR, encoding="utf-8-sig"),
            ]
        ),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        autoescape=False,
    )
    environment.globals.update(images=images)
    return environment


def _render(environment: Environment, source: Path) -> str:
    try:
        return environment.get_template(source.name).render()
    except TemplateError as error:
        die(f"{source}: {error}")


def _icon_sets(owner: Owner) -> dict[str, list[str]]:
    """Every icon set the owner ships, each as its sorted PNG names."""
    root = owner.source / IMAGES_DIR
    if not root.is_dir():
        return {}
    found = {
        directory.name: sorted(png.stem for png in directory.glob("*.png"))
        for directory in sorted(path for path in root.iterdir() if path.is_dir())
    }
    return {name: pngs for name, pngs in found.items() if pngs}


def _icons(owner: Owner, kit_root: Path, target: Path, sets: dict[str, list[str]]) -> list[Path]:
    """Copy each icon into the rendered tree beside the descriptor that names it."""
    if not sets:
        return []

    descriptor = (kit_root / VTEX_TEMPLATE).read_text(encoding="utf-8")
    written: list[Path] = []
    for icon_set, names in sets.items():
        for name in names:
            png = owner.source / IMAGES_DIR / icon_set / f"{name}.png"
            out = target / IMAGES_DIR / icon_set / f"{name}.png"
            source = f"panorama/{IMAGES_DIR}/{icon_set}/{name}.png"
            written += write(out, png.read_bytes())
            written += put(out.with_suffix(".vtex"), descriptor.replace("%SOURCE%", source))
    return written
