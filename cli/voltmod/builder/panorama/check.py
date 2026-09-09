"""Report what a screen gets wrong, before the client rejects it silently on load.

Two kinds of finding: rules the CS2 client enforces without saying so - a disallowed element, a
Button with no id, an image that resolves to nothing - and the one thing no single screen can see
for itself, which is two owners writing the same resource path.

`bind` only emits; every rule lives here, and both read the same parsed screen so they cannot
disagree about what a layout says.
"""

from pathlib import Path
from xml.etree import ElementTree

from . import bind, screens
from .screens import Owner

#: What the client's Panorama parser accepts anywhere in a layout.
ALLOWED_TAGS = {"root", "styles", "include", "Panel", "Label", "Image", "Button"}


def check(root: Path, kit_root: Path, names: list[str]) -> list[str]:
    """Every problem the named owners' screens would fail on, as `<file>: <problem>` lines."""
    owners = screens.select(screens.find_owners(root), names)
    findings: list[str] = []
    claimed: dict[str, str] = {}

    for owner in owners.values():
        for resource in _owner_images(owner):
            findings += _claim(resource, owner, claimed)
        for source in screens.sources(owner):
            findings += _screen(owner, kit_root, source, claimed)

    return findings


def _screen(owner: Owner, kit_root: Path, source: Path, claimed: dict[str, str]) -> list[str]:
    name = source.name.removesuffix(screens.SUFFIX)
    findings = _claim(f"layout/custom_game/{name}.xml", owner, claimed)
    findings += _claim(f"styles/custom_game/{name}.css", owner, claimed)

    layout, stylesheet = screens.screen(owner, kit_root, name)
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
    taken = {"Layout": "the screen itself", "RootId": "the screen itself"}

    for identifier in screen.ids:
        if not bind.spellable(identifier.removeprefix(f"{screen.name}_")):
            findings.append(f"{source}: id '{identifier}' cannot be spelled in C++")
            continue
        spelled = bind.member(identifier, screen.name)
        if spelled in taken:
            clash = taken[spelled]
            findings.append(f"{source}: id '{identifier}' and {clash} both spell {spelled}")
        taken[spelled] = f"id '{identifier}'"

    for prefix, variants in screen.families.items():
        # A digit-only family gets no enum, so its variants never have to be C++ names.
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
        icon_set, name = match.groups()
        if not (owner.source / screens.IMAGES_DIR / icon_set / f"{name}.png").is_file():
            findings.append(f"{source}: Image src '{src}' has no {icon_set}/{name}.png")
    return findings


def _owner_images(owner: Owner) -> list[str]:
    """Every image resource @p owner renders, regardless of which screen references it."""
    directory = owner.source / screens.IMAGES_DIR
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
