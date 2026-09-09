"""Validate rendered Panorama screens against the rules the CS2 client enforces silently.

`render` already refuses a binding that violates its own contract (`bind.die`s on that); this
walks the same rendered XML and CSS for what the *client* refuses - disallowed elements, a
Button with no id, an unresolved image - plus the two things no single screen can see for
itself: the interned-name budget and two owners writing the same resource path. Nothing here
writes anything; a broken screen becomes one line of output, not a crash.
"""

import re
from pathlib import Path
from xml.etree import ElementTree

from . import KIT_OWNER, Owner, find_owners, select
from . import bind as binder
from . import render as renderer

#: What the client's Panorama parser accepts anywhere in a layout.
ALLOWED_TAGS = {"root", "styles", "include", "Panel", "Label", "Image", "Button"}

#: Panel ids, class names and dialog variables share one 1024-entry interned table (custom-ui.md).
NAME_BUDGET = 900

_VAR = re.compile(r"^\{s:(\w+)\}$")
_CLASS = re.compile(r"\.([A-Za-z0-9_-]+)")
_IMAGE_SRC = re.compile(r"^s2r://panorama/images/custom_game/([^/]+)/([^/]+)\.vtex$")


def check(root: Path, kit_root: Path, names: list[str]) -> list[str]:
    """Every problem the named owners' screens would fail on, as `<file>: <problem>` lines."""
    owners = select(find_owners(root, kit_root), names)
    findings: list[str] = []
    claimed: dict[str, str] = {}

    for owner in owners.values():
        for resource in _owner_images(owner):
            findings += _claim(resource, owner, claimed)
        for source in renderer.sources(owner):
            name = source.name.removesuffix(renderer.SUFFIX)
            findings += _screen(owner, root, kit_root, name, source, claimed)

    return findings


def _screen(
    owner: Owner, root: Path, kit_root: Path, name: str, source: Path, claimed: dict[str, str]
) -> list[str]:
    findings = _claim(f"layout/custom_game/{name}.xml", owner, claimed)
    findings += _claim(f"styles/custom_game/{name}.css", owner, claimed)

    try:
        layout, stylesheet = renderer.screen(owner, root, kit_root, name)
    except SystemExit as error:
        return findings + [_die_message(error)]

    try:
        tree = ElementTree.fromstring(layout)
    except ElementTree.ParseError as error:
        return findings + [f"{source}: the rendered layout is not well-formed XML: {error}"]

    ids = [node.get("id") for node in tree.iter() if node.get("id")]
    findings += _tags(tree, source)
    findings += _buttons(tree, source)
    findings += _unique_ids(ids, source)
    findings += _prefixed_ids(ids, source)
    findings += _stylesheet_include(tree, name, source)
    findings += _images(owner, tree, source)
    findings += _budget(tree, stylesheet, source)
    findings += _binding(owner, layout, stylesheet, source)
    return findings


def _tags(tree: ElementTree.Element, source: Path) -> list[str]:
    """Only Panel/Label/Image/Button and the include scaffolding; a script node is refused."""
    return [
        f"{source}: <{node.tag}> is not an allowed element"
        for node in tree.iter()
        if node.tag not in ALLOWED_TAGS
    ]


def _buttons(tree: ElementTree.Element, source: Path) -> list[str]:
    """Every Button needs an id, and a Button inside a Button loses the inner press."""
    findings: list[str] = []
    parent = {child: node for node in tree.iter() for child in node}
    for node in tree.iter("Button"):
        if not node.get("id"):
            findings.append(f"{source}: <Button> has no id")
        ancestor = parent.get(node)
        while ancestor is not None:
            if ancestor.tag == "Button":
                findings.append(f"{source}: <Button> is nested inside another Button")
                break
            ancestor = parent.get(ancestor)
    return findings


def _unique_ids(ids: list[str], source: Path) -> list[str]:
    seen: set[str] = set()
    findings = []
    for identifier in ids:
        if identifier in seen:
            findings.append(f"{source}: id '{identifier}' is used more than once")
        seen.add(identifier)
    return findings


def _prefixed_ids(ids: list[str], source: Path) -> list[str]:
    """Every id but the screen's own has to sit under it - the same rule bind.py names by."""
    if not ids:
        return []
    screen = ids[0]
    return [
        f"{source}: id '{identifier}' does not start with '{screen}_'"
        for identifier in ids[1:]
        if not identifier.startswith(f"{screen}_")
    ]


def _stylesheet_include(tree: ElementTree.Element, name: str, source: Path) -> list[str]:
    expected = f"file://{{resources}}/styles/custom_game/{name}.css"
    includes = [node.get("src", "") for node in tree.iter("include")]
    if includes == [expected]:
        return []
    got = ", ".join(includes) if includes else "none"
    return [f"{source}: expected one style include of '{expected}', got {got}"]


def _images(owner: Owner, tree: ElementTree.Element, source: Path) -> list[str]:
    findings: list[str] = []
    for node in tree.iter("Image"):
        src = node.get("src", "")
        match = _IMAGE_SRC.match(src)
        if not match:
            findings.append(
                f"{source}: Image src '{src}' is not "
                "s2r://panorama/images/custom_game/<set>/<name>.vtex"
            )
            continue
        icon_set, icon_name = match.groups()
        png = owner.source / renderer.IMAGES_DIR / icon_set / f"{icon_name}.png"
        if not png.is_file():
            findings.append(f"{source}: Image src '{src}' has no {icon_set}/{icon_name}.png")
    return findings


def _budget(tree: ElementTree.Element, stylesheet: str, source: Path) -> list[str]:
    """Ids, distinct classes and dialog variables all share one interned-name table."""
    ids = {node.get("id") for node in tree.iter() if node.get("id")}
    classes = {cls for node in tree.iter() for cls in node.get("class", "").split()}
    classes |= set(_CLASS.findall(stylesheet))
    variables = {
        match.group(1)
        for node in tree.iter()
        for match in [_VAR.match(node.get("text", ""))]
        if match
    }

    total = len(ids) + len(classes) + len(variables)
    if total > NAME_BUDGET:
        return [f"{source}: {total} interned names exceeds the {NAME_BUDGET} budget"]
    return []


def _binding(owner: Owner, layout: str, stylesheet: str, source: Path) -> list[str]:
    """The screen has to bind, exactly as `render` would derive it - the framework has none."""
    if owner.name == KIT_OWNER:
        return []
    try:
        binder.header(layout, stylesheet, source.read_text(encoding="utf-8-sig"), source.name)
    except SystemExit as error:
        return [_die_message(error)]
    return []


def _owner_images(owner: Owner) -> list[str]:
    """Every image resource @p owner renders, regardless of which screen references it."""
    directory = owner.source / renderer.IMAGES_DIR
    if not directory.is_dir():
        return []
    resources = []
    for icon_set in sorted(path for path in directory.iterdir() if path.is_dir()):
        for png in sorted(icon_set.glob("*.png")):
            resources.append(f"images/custom_game/{icon_set.name}/{png.stem}.*")
    return resources


def _claim(resource: str, owner: Owner, claimed: dict[str, str]) -> list[str]:
    """The first owner to render @p resource keeps it; a second owner is a finding."""
    holder = claimed.get(resource)
    if holder and holder != owner.name:
        return [f"{resource}: rendered by both {holder} and {owner.name}"]
    claimed[resource] = owner.name
    return []


def _die_message(error: SystemExit) -> str:
    """A `die()` failure's message, without the `ERROR: ` prefix a finding does not need."""
    return str(error).removeprefix("ERROR: ")
