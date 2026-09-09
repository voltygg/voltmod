"""Panorama screens: who owns one, how it renders, and what it has to get right.

A screen is native Panorama XML and CSS written as Jinja templates under a plugin's
`panorama/screens/`. Nothing rendered is committed: `render` writes into `build/panorama/`, and
`compile` and `publish` read from there. Rendering a screen also derives its C++ header, so a
plugin cannot drift from the layout it names.

The framework is not an owner. It ships `panorama/blocks/`, which templates reach through the
Jinja loader rather than by owning a tree of its own.
"""

from dataclasses import dataclass
from pathlib import Path
from xml.etree import ElementTree

from jinja2 import ChoiceLoader, Environment, FileSystemLoader, StrictUndefined, TemplateError

from voltmod.tools import die

from . import bind

#: Rendered trees live here, one directory per owner, under the project's build tree.
BUILD_DIR = "build/panorama"

SCREENS_DIR = "screens"
HEADERS_DIR = "Ui"
BLOCKS_DIR = "panorama/blocks"
IMAGES_DIR = "images/custom_game"

SUFFIX = ".xml.j2"

#: What the client's Panorama parser accepts anywhere in a layout.
ALLOWED_TAGS = {"root", "styles", "include", "Panel", "Label", "Image", "Button"}

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


@dataclass(frozen=True, slots=True)
class Owner:
    """A plugin that ships a `panorama/` tree."""

    name: str
    source: Path


# --- Owners and where their output goes ---------------------------------------------------


def find_owners(root: Path) -> dict[str, Owner]:
    """Every plugin `panorama/` tree this project renders, keyed by plugin name."""
    owners: dict[str, Owner] = {}

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
        known = ", ".join(owners) or "none"
        die(f"no panorama sources for {', '.join(unknown)}\nKnown: {known}")
    return {name: owners[name] for name in names}


def output(root: Path, owner: Owner, out: Path | None = None) -> Path:
    """Where @p owner's rendered tree goes.

    The `panorama/` prefix is load-bearing: a layout's `<include src="file://{resources}/...">`
    resolves against it, so it survives into the addon and into a publish directory.
    """
    return (out or root / BUILD_DIR) / owner.name / "panorama"


def includes(root: Path, owner: Owner, out: Path | None = None) -> Path:
    """Where @p owner's derived screen headers go: what a plugin puts on its include path."""
    return (out or root / BUILD_DIR) / owner.name / "include"


def sources(owner: Owner) -> list[Path]:
    """Every screen template @p owner ships."""
    return sorted((owner.source / SCREENS_DIR).glob(f"*{SUFFIX}"))


# --- Rendering ----------------------------------------------------------------------------


def render(root: Path, kit_root: Path, names: list[str], out: Path | None = None) -> list[Path]:
    """Render the named owners' screens, and return what was written."""
    owners = select(find_owners(root), names)

    written: list[Path] = []
    for owner in owners.values():
        written += _owner(owner, root, kit_root, out)
    return written


def screen(owner: Owner, kit_root: Path, name: str) -> tuple[str, str]:
    """One screen rendered in memory: its layout and its stylesheet, without the banners."""
    environment = _environment(owner, kit_root, _icon_sets(owner), name)
    source = owner.source / SCREENS_DIR / f"{name}{SUFFIX}"
    style = owner.source / SCREENS_DIR / f"{name}.css"
    return _render(environment, source.name, source), _render(environment, style.name, style)


def _owner(owner: Owner, root: Path, kit_root: Path, out: Path | None) -> list[Path]:
    target = output(root, owner, out)

    written: list[Path] = []
    for source in sources(owner):
        name = source.name.removesuffix(SUFFIX)
        banner = bind.BANNER.format(name=name)
        layout, stylesheet = screen(owner, kit_root, name)

        layout_out = target / "layout/custom_game" / f"{name}.xml"
        styles_out = target / "styles/custom_game" / f"{name}.css"
        written += put(layout_out, f"<!-- {banner} -->\n{layout}")
        written += put(styles_out, f"/* {banner} */\n{stylesheet}")
        written += _binding(owner, root, out, source, layout, stylesheet)

    return written + _icons(owner, target, _icon_sets(owner))


def _binding(
    owner: Owner, root: Path, out: Path | None, source: Path, layout: str, stylesheet: str
) -> list[Path]:
    """The screen's C++ header, beside the rendered tree rather than in it."""
    name = source.name.removesuffix(SUFFIX)
    try:
        parsed = bind.read(layout, stylesheet)
    except ElementTree.ParseError as error:
        die(f"{source}: the rendered layout is not well-formed XML: {error}")

    text = bind.header(parsed, source.read_text(encoding="utf-8-sig"))
    return put(includes(root, owner, out) / HEADERS_DIR / f"{bind.pascal(name)}.hpp", text)


def _environment(
    owner: Owner, kit_root: Path, images: dict[str, list[str]], name: str
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
    environment.globals.update(images=images, screen=name)
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
    """Copy each icon into the rendered tree beside the descriptor that names it.

    resourcecompiler compiles the `.vtex` descriptor and never the image, so both have to be
    there.
    """
    written: list[Path] = []
    for icon_set, names in images.items():
        for name in names:
            png = owner.source / IMAGES_DIR / icon_set / f"{name}.png"
            out = target / IMAGES_DIR / icon_set / f"{name}.png"
            written += _write(out, png.read_bytes())
            descriptor = VTEX.format(source=f"panorama/{IMAGES_DIR}/{icon_set}/{name}.png")
            written += put(out.with_suffix(".vtex"), descriptor)
    return written


def put(path: Path, text: str) -> list[Path]:
    """Write @p text unless it is already there, so a second run touches nothing."""
    return _write(path, text.encode("utf-8"))


def _write(path: Path, data: bytes) -> list[Path]:
    if path.is_file() and path.read_bytes() == data:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return [path]


# --- Checking -----------------------------------------------------------------------------


def check(root: Path, kit_root: Path, names: list[str]) -> list[str]:
    """Every problem the named owners' screens would fail on, as `<file>: <problem>` lines.

    Two kinds: what the *client* refuses silently - a disallowed element, a Button with no id, an
    unresolved image - and what no single screen can see for itself, which is two owners writing
    the same resource path. Nothing here writes anything.
    """
    owners = select(find_owners(root), names)
    findings: list[str] = []
    claimed: dict[str, str] = {}

    for owner in owners.values():
        for resource in _owner_images(owner):
            findings += _claim(resource, owner, claimed)
        for source in sources(owner):
            findings += _screen(owner, kit_root, source, claimed)

    return findings


def _screen(owner: Owner, kit_root: Path, source: Path, claimed: dict[str, str]) -> list[str]:
    name = source.name.removesuffix(SUFFIX)
    findings = _claim(f"layout/custom_game/{name}.xml", owner, claimed)
    findings += _claim(f"styles/custom_game/{name}.css", owner, claimed)

    layout, stylesheet = screen(owner, kit_root, name)
    try:
        parsed = bind.read(layout, stylesheet)
    except ElementTree.ParseError as error:
        return findings + [f"{source}: the rendered layout is not well-formed XML: {error}"]

    return (
        findings
        + _tags(parsed, source)
        + _buttons(parsed, source)
        + _ids(parsed, source)
        + _names(parsed, source)
        + _stylesheet_include(parsed, name, source)
        + _images(owner, parsed, source)
    )


def _tags(screen: bind.Screen, source: Path) -> list[str]:
    """Only Panel/Label/Image/Button and the include scaffolding; a script node is refused."""
    return [
        f"{source}: <{node.tag}> is not an allowed element"
        for node in screen.tree.iter()
        if node.tag not in ALLOWED_TAGS
    ]


def _buttons(screen: bind.Screen, source: Path) -> list[str]:
    """Every Button needs an id, and a Button inside a Button loses the inner press."""
    findings: list[str] = []
    parent = {child: node for node in screen.tree.iter() for child in node}
    for node in screen.tree.iter("Button"):
        if not node.get("id"):
            findings.append(f"{source}: <Button> has no id")
        ancestor = parent.get(node)
        while ancestor is not None:
            if ancestor.tag == "Button":
                findings.append(f"{source}: <Button> is nested inside another Button")
                break
            ancestor = parent.get(ancestor)
    return findings


def _ids(screen: bind.Screen, source: Path) -> list[str]:
    """An id names the screen, or sits under it, and never repeats."""
    if not screen.name:
        return [f"{source}: no element carries an id, so there is nothing to name the screen"]

    findings: list[str] = []
    seen: set[str] = set()
    for identifier in screen.ids:
        if identifier in seen:
            findings.append(f"{source}: id '{identifier}' is used more than once")
        seen.add(identifier)
        if not identifier.startswith(f"{screen.name}_"):
            findings.append(f"{source}: id '{identifier}' does not start with '{screen.name}_'")
    return findings


def _names(screen: bind.Screen, source: Path) -> list[str]:
    """Everything the header spells has to be a C++ name, and no two may be the same one."""
    findings: list[str] = []
    taken: dict[str, str] = {"Layout": "the screen itself", "RootId": "the screen itself"}

    def claim(name: str, what: str) -> None:
        if not bind.spellable(name.removeprefix(f"{screen.name}_")):
            findings.append(f"{source}: '{name}' is not a name a C++ constant can take")
            return
        member = bind.member(name, screen.name) if what == "id" else name
        if member in taken:
            findings.append(f"{source}: {what} '{name}' and {taken[member]} both spell {member}")
        taken[member] = f"{what} '{name}'"

    for identifier in screen.ids:
        claim(identifier, "id")
    for prefix, variants in screen.families.items():
        named = bind.enumerated(variants)
        if not bind.spellable(prefix) or (named and not all(bind.spellable(v) for v in variants)):
            findings.append(f"{source}: class family '{prefix}--*' cannot be spelled in C++")
    return findings


def _stylesheet_include(screen: bind.Screen, name: str, source: Path) -> list[str]:
    expected = f"file://{{resources}}/styles/custom_game/{name}.css"
    found = [node.get("src", "") for node in screen.tree.iter("include")]
    if found == [expected]:
        return []
    got = ", ".join(found) or "none"
    return [f"{source}: expected one style include of '{expected}', got {got}"]


def _images(owner: Owner, screen: bind.Screen, source: Path) -> list[str]:
    findings: list[str] = []
    for node in screen.tree.iter("Image"):
        src = node.get("src", "")
        match = bind.IMAGE_SRC.match(src)
        if not match:
            findings.append(
                f"{source}: Image src '{src}' is not "
                "s2r://panorama/images/custom_game/<set>/<name>.vtex"
            )
            continue
        icon_set, icon_name = match.groups()
        if not (owner.source / IMAGES_DIR / icon_set / f"{icon_name}.png").is_file():
            findings.append(f"{source}: Image src '{src}' has no {icon_set}/{icon_name}.png")
    return findings


def _owner_images(owner: Owner) -> list[str]:
    """Every image resource @p owner renders, regardless of which screen references it."""
    directory = owner.source / IMAGES_DIR
    if not directory.is_dir():
        return []
    return [
        f"images/custom_game/{icon_set.name}/{png.stem}.*"
        for icon_set in sorted(path for path in directory.iterdir() if path.is_dir())
        for png in sorted(icon_set.glob("*.png"))
    ]


def _claim(resource: str, owner: Owner, claimed: dict[str, str]) -> list[str]:
    """The first owner to render @p resource keeps it; a second owner is a finding."""
    holder = claimed.get(resource)
    if holder and holder != owner.name:
        return [f"{resource}: rendered by both {holder} and {owner.name}"]
    claimed[resource] = owner.name
    return []
