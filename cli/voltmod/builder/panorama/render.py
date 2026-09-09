"""Render an owner's screens into the build tree.

One screen is `screens/<name>.xml.j2` plus `screens/<name>.css`, both Jinja templates over the
theme, plus whatever `panorama/skin/<name>.css` the project appends. Icon PNGs are copied beside
a generated `.vtex`, because resourcecompiler compiles the descriptor and never the image.
"""

from pathlib import Path
from typing import Any

from jinja2 import ChoiceLoader, Environment, FileSystemLoader, StrictUndefined, TemplateError

from voltmod.tools import die

from . import Owner, find_owners, output, select
from . import theme as theme_module

SCREENS_DIR = "screens"
BLOCKS_DIR = "panorama/blocks"
SKIN_DIR = "panorama/skin"
IMAGES_DIR = "images/custom_game"

SUFFIX = ".xml.j2"
BANNER = "Rendered by `voltmod panorama render` from panorama/screens/. Do not edit."

#: The keyvalues2 dialect the Workshop Tools write. One leading comment only: the DMX reader
#: takes the encoding line and nothing else, so this carries no banner.
VTEX = """<!-- dmx encoding keyvalues2_noids 1 format vtex 1 -->
"CDmeVtex"
{{
\t"m_inputTextureArray" "element_array"
\t[
\t\t"CDmeInputTexture"
\t\t{{
\t\t\t"m_name" "string" "InputTexture0"
\t\t\t"m_fileName" "string" "{source}"
\t\t\t"m_colorSpace" "string" "srgb"
\t\t\t"m_typeString" "string" "2D"
\t\t\t"m_imageProcessorArray" "element_array"
\t\t\t[
\t\t\t\t"CDmeImageProcessor"
\t\t\t\t{{
\t\t\t\t\t"m_algorithm" "string" "None"
\t\t\t\t\t"m_stringArg" "string" ""
\t\t\t\t\t"m_vFloat4Arg" "vector4" "0 0 0 0"
\t\t\t\t}}
\t\t\t]
\t\t}}
\t]
\t"m_outputTypeString" "string" "2D"
\t"m_outputFormat" "string" "BGRA8888"
\t"m_outputClearColor" "vector4" "0 0 0 0"
\t"m_nOutputMinDimension" "int" "0"
\t"m_nOutputMaxDimension" "int" "1024"
\t"m_textureOutputChannelArray" "element_array"
\t[
\t\t"CDmeTextureOutputChannel"
\t\t{{
\t\t\t"m_inputTextureArray" "string_array" [ "InputTexture0" ]
\t\t\t"m_srcChannels" "string" "rgba"
\t\t\t"m_dstChannels" "string" "rgba"
\t\t\t"m_mipAlgorithm" "CDmeImageProcessor"
\t\t\t{{
\t\t\t\t"m_algorithm" "string" "Box"
\t\t\t\t"m_stringArg" "string" ""
\t\t\t\t"m_vFloat4Arg" "vector4" "0 0 0 0"
\t\t\t}}
\t\t\t"m_outputColorSpace" "string" "srgb"
\t\t}}
\t]
\t"m_vClamp" "vector3" "0 0 0"
\t"m_bNoLod" "bool" "1"
}}
"""


def render(root: Path, kit_root: Path, names: list[str], out: Path | None = None) -> list[Path]:
    """Render the named owners' screens, and return what was written."""
    owners = select(find_owners(root, kit_root), names)
    palette = theme_module.load(root, kit_root)

    written: list[Path] = []
    for owner in owners.values():
        written += _owner(owner, root, kit_root, palette, out)
    return written


def _owner(
    owner: Owner, root: Path, kit_root: Path, palette: dict[str, Any], out: Path | None
) -> list[Path]:
    target = output(root, owner, out)
    images = _icon_sets(owner)

    written: list[Path] = []
    for source in sorted((owner.source / SCREENS_DIR).glob(f"*{SUFFIX}")):
        screen = source.name.removesuffix(SUFFIX)
        environment = _environment(owner, kit_root, palette, images, screen)
        written += _put(
            target / "layout/custom_game" / f"{screen}.xml",
            f"<!-- {BANNER} -->\n" + _render(environment, source.name, source),
        )
        written += _put(
            target / "styles/custom_game" / f"{screen}.css",
            f"/* {BANNER} */\n" + _stylesheet(environment, owner, root, screen),
        )

    return written + _icons(owner, target, images)


def _stylesheet(environment: Environment, owner: Owner, root: Path, screen: str) -> str:
    """The screen's own stylesheet, with the project's skin for it appended."""
    source = owner.source / SCREENS_DIR / f"{screen}.css"
    text = _render(environment, source.name, source)

    skin = root / SKIN_DIR / f"{screen}.css"
    if skin.is_file():
        rendered = _render_text(environment, skin.read_text(encoding="utf-8-sig"), skin)
        text = f"{text.rstrip()}\n\n/* {SKIN_DIR}/{screen}.css */\n{rendered}"
    return text


def _environment(
    owner: Owner, kit_root: Path, palette: dict[str, Any], images: dict[str, list[str]], screen: str
) -> Environment:
    """Templates resolve against the owner's screens first, then the framework's block library.

    The context is global rather than passed per render so that a block imported with
    `{% import %}` sees the theme, the icon sets, and the screen name too.
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
    environment.globals.update(palette, images=images, screen=screen)
    return environment


def _render(environment: Environment, name: str, where: Path) -> str:
    try:
        return environment.get_template(name).render()
    except TemplateError as error:
        die(f"{where}: {error}")


def _render_text(environment: Environment, text: str, where: Path) -> str:
    try:
        return environment.from_string(text).render()
    except TemplateError as error:
        die(f"{where}: {error}")


def _icon_sets(owner: Owner) -> dict[str, list[str]]:
    """Every icon set the owner ships, each as its sorted PNG names."""
    root = owner.source / IMAGES_DIR
    if not root.is_dir():
        return {}

    found: dict[str, list[str]] = {}
    for directory in sorted(path for path in root.iterdir() if path.is_dir()):
        names = sorted(png.stem for png in directory.glob("*.png"))
        if names:
            found[directory.name] = names
    return found


def _icons(owner: Owner, target: Path, images: dict[str, list[str]]) -> list[Path]:
    """Copy each icon into the rendered tree beside the descriptor that names it."""
    written: list[Path] = []
    for icon_set, names in images.items():
        for name in names:
            png = owner.source / IMAGES_DIR / icon_set / f"{name}.png"
            out = target / IMAGES_DIR / icon_set / f"{name}.png"
            written += _write(out, png.read_bytes())
            source = f"panorama/{IMAGES_DIR}/{icon_set}/{name}.png"
            written += _put(out.with_suffix(".vtex"), VTEX.format(source=source))
    return written


def _put(path: Path, text: str) -> list[Path]:
    return _write(path, text.encode("utf-8"))


def _write(path: Path, data: bytes) -> list[Path]:
    """Write @p data unless it is already there, so a second render touches nothing."""
    if path.is_file() and path.read_bytes() == data:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return [path]
