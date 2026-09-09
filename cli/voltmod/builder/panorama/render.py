"""Render an owner's screens into the build tree.

One screen is `screens/<name>.xml.j2` plus `screens/<name>.css`, Jinja over the framework's block
library, plus whatever plain `panorama/skin/<name>.css` the project appends. Rendering a screen
also derives its C++ binding, so a plugin's header cannot drift from the layout it names. Icon
PNGs are copied beside a generated `.vtex`, because resourcecompiler compiles the descriptor and
never the image.
"""

from pathlib import Path

from jinja2 import ChoiceLoader, Environment, FileSystemLoader, StrictUndefined, TemplateError

from voltmod.tools import die

from . import KIT_OWNER, Owner, find_owners, includes, output, select
from . import bind as binder

SCREENS_DIR = "screens"
HEADERS_DIR = "Ui"
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

    written: list[Path] = []
    for owner in owners.values():
        written += _owner(owner, root, kit_root, out)
    return written


def sources(owner: Owner) -> list[Path]:
    """Every screen template @p owner ships."""
    return sorted((owner.source / SCREENS_DIR).glob(f"*{SUFFIX}"))


def screen(owner: Owner, root: Path, kit_root: Path, name: str) -> tuple[str, str]:
    """One screen rendered in memory: its layout and its stylesheet, without the banners."""
    environment = _environment(owner, kit_root, _icon_sets(owner), name)
    source = owner.source / SCREENS_DIR / f"{name}{SUFFIX}"
    return _render(environment, source.name, source), _stylesheet(environment, owner, root, name)


def _owner(owner: Owner, root: Path, kit_root: Path, out: Path | None) -> list[Path]:
    target = output(root, owner, out)

    written: list[Path] = []
    for source in sources(owner):
        name = source.name.removesuffix(SUFFIX)
        layout, stylesheet = screen(owner, root, kit_root, name)
        written += put(
            target / "layout/custom_game" / f"{name}.xml", f"<!-- {BANNER} -->\n{layout}"
        )
        written += put(
            target / "styles/custom_game" / f"{name}.css", f"/* {BANNER} */\n{stylesheet}"
        )
        written += _binding(owner, root, out, source, layout, stylesheet)

    return written + _icons(owner, target, _icon_sets(owner))


def _binding(
    owner: Owner, root: Path, out: Path | None, source: Path, layout: str, stylesheet: str
) -> list[Path]:
    """The screen's C++ binding. The framework has no plugin of its own to bind one into."""
    if owner.name == KIT_OWNER:
        return []

    name = binder.pascal(source.name.removesuffix(SUFFIX))
    text = binder.header(layout, stylesheet, source.read_text(encoding="utf-8-sig"), source.name)
    return put(includes(root, owner, out) / HEADERS_DIR / f"{name}.hpp", text)


def _stylesheet(environment: Environment, owner: Owner, root: Path, screen: str) -> str:
    """The screen's own stylesheet, with the project's plain-CSS skin for it appended."""
    source = owner.source / SCREENS_DIR / f"{screen}.css"
    text = _render(environment, source.name, source)

    skin = root / SKIN_DIR / f"{screen}.css"
    if skin.is_file():
        text = f"{text.rstrip()}\n\n/* {SKIN_DIR}/{screen}.css */\n" + skin.read_text(
            encoding="utf-8-sig"
        )
    return text


def _environment(
    owner: Owner, kit_root: Path, images: dict[str, list[str]], screen: str
) -> Environment:
    """Templates resolve against the owner's screens first, then the framework's block library.

    The context is global rather than passed per render so that a block imported with
    `{% import %}` sees the icon sets and the screen name too.
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
    environment.globals.update(images=images, screen=screen)
    return environment


def _render(environment: Environment, name: str, where: Path) -> str:
    try:
        return environment.get_template(name).render()
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
            written += put(out.with_suffix(".vtex"), VTEX.format(source=source))
    return written


def put(path: Path, text: str) -> list[Path]:
    """Write @p text unless it is already there, so a second run touches nothing."""
    return _write(path, text.encode("utf-8"))


def _write(path: Path, data: bytes) -> list[Path]:
    """Write @p data unless it is already there, so a second render touches nothing."""
    if path.is_file() and path.read_bytes() == data:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return [path]
