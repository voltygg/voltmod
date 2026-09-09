"""Panorama screens: who owns one, where it renders to, and how it reaches a client.

A screen is native Panorama XML and CSS written as Jinja templates under `panorama/screens/`.
Nothing rendered is committed: `render` writes into `build/panorama/`, and `compile` and
`publish` read from there.
"""

from dataclasses import dataclass
from pathlib import Path

from voltmod.tools import die

#: Rendered trees live here, one directory per owner, under the project's build tree.
BUILD_DIR = "build/panorama"


@dataclass(frozen=True, slots=True)
class Owner:
    """Whoever owns a `panorama/` tree: the framework itself, or one plugin."""

    name: str
    source: Path


def find_owners(root: Path, kit_root: Path) -> dict[str, Owner]:
    """Every `panorama/` tree this project renders, keyed by its owner.

    The project's own `panorama/` is not one: it holds the theme and the skin stylesheets that
    every owner's screens are rendered against.
    """
    owners = {"voltmod": Owner("voltmod", kit_root / "panorama")}

    plugins = root / "plugins"
    if plugins.is_dir():
        for plugin in sorted(plugins.iterdir()):
            directory = plugin / "panorama"
            if directory.is_dir():
                owners[plugin.name] = Owner(plugin.name, directory)

    return owners


def select(owners: dict[str, Owner], names: list[str]) -> dict[str, Owner]:
    """The named owners, or all of them when nothing is named."""
    if not names:
        return owners
    unknown = [name for name in names if name not in owners]
    if unknown:
        die(f"no panorama sources for {', '.join(unknown)}\nKnown: {', '.join(owners)}")
    return {name: owners[name] for name in names}


def output(root: Path, owner: Owner, out: Path | None = None) -> Path:
    """Where @p owner's rendered tree goes.

    The `panorama/` prefix is load-bearing: a layout's `<include src="file://{resources}/...">`
    resolves against it, so it survives into the addon and into a publish directory.
    """
    return (out or root / BUILD_DIR) / owner.name / "panorama"
