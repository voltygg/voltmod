"""Where each plugin keeps its Panorama screens and icons, and where they render to."""

from dataclasses import dataclass
from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.project import PLUGIN_DIRS

BUILD_DIR = "build/panorama"
SCREENS_DIR = "screens"
IMAGES_DIR = "images"
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
    """The PNG behind `s2r://panorama/images/<set>/<name>.vtex`."""
    return owner.source / IMAGES_DIR / icon_set / f"{name}.png"
