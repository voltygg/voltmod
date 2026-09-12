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

#: Every distinct id, variable and class name is interned for good in the client's table, and the
#: table is shared by every screen it loads. Overflowing it breaks rendering on a player's machine,
#: where no build step can see it.
NAME_TABLE = 1024

#: One screen past this many names is a runaway loop, not a design.
RUNAWAY_NAMES = 400


def check(root: Path, framework_root: Path, names: list[str]) -> list[str]:
    """Every problem the named owners' screens would fail on, as `<file>: <problem>` lines."""
    findings: list[str] = []
    claimed: dict[str, str] = {}
    interned: dict[Path, set[str]] = {}

    for owner in screens.select(screens.find_owners(root), names).values():
        renderer = screens.Renderer(owner, framework_root)
        for icon_set, icons in renderer.icons.items():
            for icon in icons:
                findings += _claim(f"images/custom_game/{icon_set}/{icon}.*", owner, claimed)
        for source in screens.sources(owner):
            findings += _screen(renderer, source, claimed, interned)

    return findings + _table(interned)


def _screen(
    renderer: screens.Renderer,
    source: Path,
    claimed: dict[str, str],
    interned: dict[Path, set[str]],
) -> list[str]:
    owner, name = renderer.owner, screens.stem(source)
    findings = _claim(f"layout/custom_game/{name}.xml", owner, claimed)
    findings += _claim(f"styles/custom_game/{name}.css", owner, claimed)

    layout, stylesheet = renderer.screen(name)
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
        + _budget(parsed, stylesheet, source, interned)
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

    def take(spelled: str, what: str) -> None:
        if spelled in taken:
            findings.append(f"{source}: {taken[spelled]} and {what} both spell {spelled}")
        taken[spelled] = what

    for identifier in screen.ids:
        if not bind.spellable(identifier.removeprefix(f"{screen.name}_")):
            findings.append(f"{source}: id '{identifier}' cannot be spelled in C++")
        elif identifier not in screen.grouped_ids:
            take(bind.member(identifier, screen.name), f"id '{identifier}'")

    for group in screen.groups:
        take(group.struct, f"the {group.stem} block")
        take(group.array, f"the {group.stem} block's array")
        members = group.members()
        for name in sorted(set(members)):
            if members.count(name) > 1:
                findings.append(f"{source}: the {group.stem} block names {name} twice")

    for prefix, variants in screen.families.items():
        # A digit-only family gets no enum, so its variants never have to be C++ names.
        named = bind.enumerated(variants)
        if not bind.spellable(prefix) or (named and not all(bind.spellable(v) for v in variants)):
            findings.append(f"{source}: class family '{prefix}--*' cannot be spelled in C++")
        spelled = (prefix, f"{prefix}Names", f"{prefix}Classes") if named else (f"{prefix}Classes",)
        for one in spelled:
            take(one, f"the {prefix} family")
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
        elif not screens.icon(owner, *match.groups()).is_file():
            icon_set, name = match.groups()
            findings.append(f"{source}: Image src '{src}' has no {icon_set}/{name}.png")
    return findings


def _budget(
    screen: bind.Screen, stylesheet: str, source: Path, interned: dict[Path, set[str]]
) -> list[str]:
    """Record what @p screen interns, and flag a screen that has clearly run away on its own."""
    names = {screen.name, *screen.ids, *screen.variables, *bind.selector_classes(stylesheet)}
    for node in screen.tree.iter():
        names.update(node.get("class", "").split())
    interned[source] = names

    if len(names) <= RUNAWAY_NAMES:
        return []
    return [f"{source}: {len(names)} interned names, over the per-screen limit of {RUNAWAY_NAMES}"]


def _table(interned: dict[Path, set[str]]) -> list[str]:
    """The client interns one table for every screen it loads, so the total is what overflows."""
    total = set().union(*interned.values())
    if len(total) <= NAME_TABLE:
        return []

    worst = sorted(interned.items(), key=lambda pair: len(pair[1]), reverse=True)
    blame = ", ".join(f"{source} {len(names)}" for source, names in worst)
    return [
        f"{len(total)} interned names across all screens, over the client's {NAME_TABLE}: {blame}"
    ]


def _claim(resource: str, owner: Owner, claimed: dict[str, str]) -> list[str]:
    """The first owner to render @p resource keeps it; a second owner is a finding."""
    holder = claimed.setdefault(resource, owner.name)
    if holder == owner.name:
        return []
    return [f"{resource}: rendered by both {holder} and {owner.name}"]
