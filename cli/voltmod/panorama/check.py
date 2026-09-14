"""Checks for the rules the CS2 client enforces on a screen without saying so."""

from pathlib import Path

from voltmod.check_results import CheckResult
from voltmod.errors import VoltmodError
from voltmod.panorama.layout import (
    IMAGE_SOURCE,
    Screen,
    is_cpp_name,
    member_name,
    read_screen,
    selector_classes,
)
from voltmod.panorama.render import (
    ScreenOwner,
    ScreenRenderer,
    icon_path,
    screen_name,
    screen_owners,
    screen_sources,
)

# Icons the client ships itself; there is no file of ours to resolve.
GAME_ICONS = "s2r://panorama/images/icons/"

# What the client's Panorama parser accepts anywhere in a layout.
ALLOWED_ELEMENTS = {"root", "styles", "include", "Panel", "Label", "Image", "Button"}

# The client interns every id, variable and class name in one table shared by all screens, and
# overflowing it breaks rendering on players' machines, where no build step can see it.
NAME_TABLE_SIZE = 1024

# One screen past this many names is a runaway loop, not a design.
MAX_NAMES_PER_SCREEN = 400


def check_screens(root: Path, names: list[str] | None = None) -> list[CheckResult]:
    """Every problem the named owners' screens would fail on in the client."""
    problems: list[str] = []
    claimed: dict[str, str] = {}
    interned: dict[Path, set[str]] = {}

    for owner in screen_owners(root, names):
        renderer = ScreenRenderer(owner)
        for icon_set, icons in renderer.icons.items():
            for icon in icons:
                resource = f"images/custom_game/{icon_set}/{icon}.*"
                problems += _claim_resource(resource, owner, claimed)
        for source in screen_sources(owner):
            problems += _check_screen(renderer, source, claimed, interned)

    problems += _check_name_table(interned)
    return [CheckResult(problem) for problem in problems]


def _check_screen(
    renderer: ScreenRenderer,
    source: Path,
    claimed: dict[str, str],
    interned: dict[Path, set[str]],
) -> list[str]:
    owner, name = renderer.owner, screen_name(source)
    problems = _claim_resource(f"layout/custom_game/{name}.xml", owner, claimed)
    problems += _claim_resource(f"styles/custom_game/{name}.css", owner, claimed)

    layout, stylesheet = renderer.render(name)
    try:
        screen = read_screen(layout, stylesheet, source)
    except VoltmodError as error:
        return problems + [str(error)]

    return (
        problems
        + _check_elements(screen, source)
        + _check_buttons(screen, source)
        + _check_ids(screen, source)
        + _check_cpp_names(screen, source)
        + _check_stylesheet_include(screen, name, source)
        + _check_images(owner, screen, source)
        + _check_screen_names(screen, stylesheet, source, interned)
    )


def _check_elements(screen: Screen, source: Path) -> list[str]:
    return [
        f"{source}: <{node.tag}> is not an allowed element"
        for node in screen.tree.iter()
        if node.tag not in ALLOWED_ELEMENTS
    ]


def _check_buttons(screen: Screen, source: Path) -> list[str]:
    """Every Button needs an id, and a Button inside a Button loses the inner press."""
    problems: list[str] = []
    parents = {child: node for node in screen.tree.iter() for child in node}
    for button in screen.tree.iter("Button"):
        if not button.get("id"):
            problems.append(f"{source}: <Button> has no id")
        ancestor = parents.get(button)
        while ancestor is not None:
            if ancestor.tag == "Button":
                problems.append(f"{source}: <Button> is nested inside another Button")
                break
            ancestor = parents.get(ancestor)
    return problems


def _check_ids(screen: Screen, source: Path) -> list[str]:
    """An id names the screen or starts with its name, and never repeats."""
    if not screen.name:
        return [f"{source}: no element carries an id, so there is nothing to name the screen"]

    problems: list[str] = []
    seen: set[str] = set()
    for identifier in screen.ids:
        if identifier in seen:
            problems.append(f"{source}: id '{identifier}' is used more than once")
        seen.add(identifier)
        if not identifier.startswith(f"{screen.name}_"):
            problems.append(f"{source}: id '{identifier}' does not start with '{screen.name}_'")
    return problems


def _check_cpp_names(screen: Screen, source: Path) -> list[str]:
    """Everything the header spells must be a C++ name, and no two may spell the same one."""
    problems: list[str] = []
    taken = {"Layout": "the screen itself", "RootId": "the screen itself"}

    def take(spelled: str, what: str) -> None:
        if spelled in taken:
            problems.append(f"{source}: {taken[spelled]} and {what} both spell {spelled}")
        taken[spelled] = what

    for identifier in screen.ids:
        if not is_cpp_name(identifier.removeprefix(f"{screen.name}_")):
            problems.append(f"{source}: id '{identifier}' cannot be spelled in C++")
        elif identifier not in screen.block_ids:
            take(member_name(identifier, screen.name), f"id '{identifier}'")

    for block in screen.blocks:
        take(block.struct_name, f"the {block.name} block")
        take(block.array_name, f"the {block.name} block's array")
        members = block.members()
        for member in sorted(set(members)):
            if members.count(member) > 1:
                problems.append(f"{source}: the {block.name} block names {member} twice")

    for family in screen.families:
        if not is_cpp_name(family):
            problems.append(f"{source}: class family '{family}--*' cannot be spelled in C++")
        take(f"{family}Classes", f"the {family} family")
    return problems


def _check_stylesheet_include(screen: Screen, name: str, source: Path) -> list[str]:
    expected = f"file://{{resources}}/styles/custom_game/{name}.css"
    found = [node.get("src", "") for node in screen.tree.iter("include")]
    if found == [expected]:
        return []
    got = ", ".join(found) or "none"
    return [f"{source}: expected one style include of '{expected}', got {got}"]


def _check_images(owner: ScreenOwner, screen: Screen, source: Path) -> list[str]:
    problems: list[str] = []
    for image in screen.tree.iter("Image"):
        src = image.get("src", "")
        if src.startswith(GAME_ICONS):
            continue
        match = IMAGE_SOURCE.match(src)
        if not match:
            problems.append(
                f"{source}: Image src '{src}' is neither "
                "s2r://panorama/images/custom_game/<set>/<name>.vtex "
                "nor a game icon under s2r://panorama/images/icons/"
            )
        elif not icon_path(owner, *match.groups()).is_file():
            icon_set, name = match.groups()
            problems.append(f"{source}: Image src '{src}' has no {icon_set}/{name}.png")
    return problems


def _check_screen_names(
    screen: Screen, stylesheet: str, source: Path, interned: dict[Path, set[str]]
) -> list[str]:
    """Record the names @p screen interns, and flag a screen that has run away on its own."""
    names = {screen.name, *screen.ids, *screen.variables, *selector_classes(stylesheet)}
    for node in screen.tree.iter():
        names.update(node.get("class", "").split())
    interned[source] = names

    if len(names) <= MAX_NAMES_PER_SCREEN:
        return []
    limit = MAX_NAMES_PER_SCREEN
    return [f"{source}: {len(names)} interned names, over the per-screen limit of {limit}"]


def _check_name_table(interned: dict[Path, set[str]]) -> list[str]:
    total = set().union(*interned.values())
    if len(total) <= NAME_TABLE_SIZE:
        return []
    largest = sorted(interned.items(), key=lambda pair: len(pair[1]), reverse=True)
    breakdown = ", ".join(f"{source} {len(names)}" for source, names in largest)
    return [
        f"{len(total)} interned names across all screens, "
        f"over the client's {NAME_TABLE_SIZE}: {breakdown}"
    ]


def _claim_resource(resource: str, owner: ScreenOwner, claimed: dict[str, str]) -> list[str]:
    """The first owner to render @p resource keeps it; a second owner is a problem."""
    holder = claimed.setdefault(resource, owner.name)
    if holder == owner.name:
        return []
    return [f"{resource}: rendered by both {holder} and {owner.name}"]
