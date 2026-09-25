import shutil
from pathlib import Path

from jinja2 import (
    ChoiceLoader,
    Environment,
    FileSystemLoader,
    PrefixLoader,
    StrictUndefined,
    TemplateError,
    TemplateRuntimeError,
)

from voltmod.bundled import BUNDLED_DIR, load_template
from voltmod.errors import VoltmodError
from voltmod.files import write_if_changed
from voltmod.panorama.layout import (
    Screen,
    header_namespace,
    member_name,
    pascal_case,
    read_screen,
)
from voltmod.panorama.sources import (
    ADDON_IMAGES_DIR,
    BUILD_DIR,
    IMAGES_DIR,
    LAYOUT_SUFFIX,
    SCREENS_DIR,
    STYLESHEET_SUFFIX,
    Plugin,
    header_dir,
    icon_path,
    icon_sets,
    panorama_plugins,
    rendered_dir,
    screen_name,
    screen_templates,
)

PLUGIN_TEMPLATES_DIR = "templates"
HEADERS_DIR = "Ui"
PLUGIN_TEMPLATE_PREFIX = "@"


def render_screens(
    root: Path, names: list[str] | None = None, out: Path | None = None
) -> list[Path]:
    """Render the named plugins' screens, and return the files that changed."""
    written: list[Path] = []
    all_plugins = panorama_plugins(root)
    for plugin in panorama_plugins(root, names):
        written += ScreenRenderer(plugin, all_plugins).write(root, out)
    if out is None:
        written += _remove_old_plugins(root / BUILD_DIR, all_plugins)
    return written


def screen_header(screen: Screen) -> str:
    """The C++ header naming `screen`'s panels, variables, blocks and choice modifiers."""
    return load_template("panorama/screen.hpp.j2").render(
        screen=screen,
        namespace=header_namespace(screen),
        member_name=member_name,
        pascal_case=pascal_case,
    )


def _remove_stale(trees: list[Path], keep: set[Path]) -> list[Path]:
    """Delete and return files under `trees` not in `keep`; the compiler would ship them."""
    # fmt: off
    stale = [
        path
        for tree in trees if tree.is_dir()
        for path in tree.rglob("*") if path.is_file() and path not in keep
    ]
    # fmt: on
    for path in stale:
        path.unlink()
    return stale


def _remove_old_plugins(build: Path, plugins: list[Plugin]) -> list[Path]:
    """Delete what a plugin that no longer ships screens left under `build`."""
    if not build.is_dir():
        return []
    names = {plugin.name for plugin in plugins}
    removed: list[Path] = []
    for directory in build.iterdir():
        if directory.is_dir() and directory.name not in names:
            removed += [path for path in directory.rglob("*") if path.is_file()]
            shutil.rmtree(directory)
    return removed


def _template_loader(plugin: Plugin, plugins: list[Plugin]) -> ChoiceLoader:
    """Screen-local, explicitly namespaced plugin, then bundled framework templates."""
    plugin_templates = {
        f"{PLUGIN_TEMPLATE_PREFIX}{candidate.name}": FileSystemLoader(
            candidate.panorama_dir / PLUGIN_TEMPLATES_DIR, encoding="utf-8-sig"
        )
        for candidate in plugins
        if (candidate.panorama_dir / PLUGIN_TEMPLATES_DIR).is_dir()
    }
    return ChoiceLoader(
        [
            FileSystemLoader(plugin.panorama_dir / SCREENS_DIR, encoding="utf-8-sig"),
            PrefixLoader(plugin_templates),
            FileSystemLoader(BUNDLED_DIR / "panorama/blocks", encoding="utf-8-sig"),
        ]
    )


class ScreenRenderer:
    """One plugin's screens through one Jinja environment, so the block library compiles once."""

    def __init__(self, plugin: Plugin, plugins: list[Plugin]) -> None:
        self.plugin = plugin
        self.icons = icon_sets(plugin)
        # No trimming or escaping: layouts and stylesheets come out exactly as written.
        self.environment = Environment(
            loader=_template_loader(plugin, plugins),
            undefined=StrictUndefined,
            keep_trailing_newline=True,
            autoescape=False,
        )
        # Globals, because a block reached through `{% import %}` cannot see the caller's variables.
        self.environment.globals["png_icons"] = self._png_icons
        self.environment.globals["game_icons"] = _game_icons

    def render(self, name: str) -> tuple[str, str]:
        self.environment.globals["screen"] = name
        return (
            self._render_template(f"{name}{LAYOUT_SUFFIX}"),
            self._render_template(f"{name}{STYLESHEET_SUFFIX}"),
        )

    def write(self, root: Path, out: Path | None) -> list[Path]:
        """Render every screen, header and icon into the build tree; return what changed."""
        target = rendered_dir(root, self.plugin, out)
        headers = header_dir(root, self.plugin, out) / HEADERS_DIR

        files: dict[Path, str | bytes] = {}
        for source in screen_templates(self.plugin):
            name = screen_name(source)
            layout, stylesheet = self.render(name)
            files[target / "layout/custom_game" / f"{name}.xml"] = layout
            files[target / "styles/custom_game" / f"{name}.css"] = stylesheet
            header = screen_header(read_screen(layout, stylesheet, source))
            files[headers / f"{pascal_case(name)}.hpp"] = header
        files.update(self._icon_files(target))

        written = [path for path, data in files.items() if write_if_changed(path, data)]
        return written + _remove_stale([target, headers], set(files))

    def _render_template(self, template: str) -> str:
        try:
            return self.environment.get_template(template).render()
        except TemplateError as error:
            raise VoltmodError(
                f"{self.plugin.panorama_dir / SCREENS_DIR / template}: {error}"
            ) from None

    def _png_icons(self, icon_set: str) -> list[tuple[str, str]]:
        """An icon set's `(name, src)` pairs, one per PNG under images/<icon_set>/."""
        if icon_set not in self.icons:
            raise TemplateRuntimeError(f"no PNGs under {IMAGES_DIR}/{icon_set}/")
        return [
            (name, f"s2r://panorama/{ADDON_IMAGES_DIR}/{icon_set}/{name}.vtex")
            for name in self.icons[icon_set]
        ]

    def _icon_files(self, target: Path) -> dict[Path, str | bytes]:
        # resourcecompiler compiles the .vtex descriptor, which names the PNG beside it.
        descriptor = load_template("panorama/icon.vtex.j2")
        files: dict[Path, str | bytes] = {}
        for icon_set, names in self.icons.items():
            for name in names:
                png = target / ADDON_IMAGES_DIR / icon_set / f"{name}.png"
                source = f"panorama/{ADDON_IMAGES_DIR}/{icon_set}/{name}.png"
                files[png] = icon_path(self.plugin, icon_set, name).read_bytes()
                files[png.with_suffix(".vtex")] = descriptor.render(source=source)
        return files


def _game_icons(pairs: list[tuple[str, str]], folder: str = "ui") -> list[tuple[str, str]]:
    """`(name, icon)` pairs of built-in CS2 icons as `(name, src)`; `folder` is under icons/."""
    return [(name, f"s2r://panorama/images/icons/{folder}/{icon}.vsvg") for name, icon in pairs]
