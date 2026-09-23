"""Where each plugin keeps its Panorama screens and icons, and where they render to."""

from pathlib import Path

from voltmod.errors import VoltmodError
from voltmod.project import Plugin, Project

BUILD_DIR = "build/panorama"
SCREENS_DIR = "screens"
IMAGES_DIR = "images"
LAYOUT_SUFFIX = ".xml.j2"
STYLESHEET_SUFFIX = ".css.j2"


def panorama_plugins(root: Path, names: list[str] | None = None) -> list[Plugin]:
    """The named plugins that ship a panorama/ tree, or all of them when none is named."""
    found = {
        plugin.name: plugin
        for plugin in Project(root).plugins(include_tools=True)
        if plugin.panorama_dir.is_dir()
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


def rendered_dir(root: Path, plugin: Plugin, out: Path | None = None) -> Path:
    # Keeps the panorama/ prefix: a layout's `file://{resources}/...` include resolves against it.
    return (out or root / BUILD_DIR) / plugin.name / "panorama"


def header_dir(root: Path, plugin: Plugin, out: Path | None = None) -> Path:
    """What a plugin puts on its include path for its screen headers."""
    return (out or root / BUILD_DIR) / plugin.name / "include"


def screen_templates(plugin: Plugin) -> list[Path]:
    return sorted((plugin.panorama_dir / SCREENS_DIR).glob(f"*{LAYOUT_SUFFIX}"))


def screen_name(source: Path) -> str:
    """`hud.xml.j2` -> `hud`."""
    return source.name.removesuffix(LAYOUT_SUFFIX)


def icon_sets(plugin: Plugin) -> dict[str, list[str]]:
    """Every icon set the plugin ships, as its sorted PNG names; empty sets are skipped."""
    images = plugin.panorama_dir / IMAGES_DIR
    if not images.is_dir():
        return {}
    return {
        directory.name: names
        for directory in sorted(path for path in images.iterdir() if path.is_dir())
        if (names := sorted(png.stem for png in directory.glob("*.png")))
    }


def icon_path(plugin: Plugin, icon_set: str, name: str) -> Path:
    """The PNG behind `s2r://panorama/images/<set>/<name>.vtex`."""
    return plugin.panorama_dir / IMAGES_DIR / icon_set / f"{name}.png"
