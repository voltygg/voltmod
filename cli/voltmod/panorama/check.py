"""Checks for the rules the CS2 client enforces on a screen without saying so."""

from pathlib import Path

from voltmod.checks.results import CheckResult
from voltmod.errors import VoltmodError
from voltmod.panorama.layout import (
    IMAGE_SOURCE,
    Screen,
    is_cpp_name,
    member_name,
    pascal_case,
    read_screen,
    selector_classes,
)
from voltmod.panorama.render import ScreenRenderer
from voltmod.panorama.sources import (
    Plugin,
    icon_path,
    panorama_plugins,
    screen_name,
    screen_templates,
)

# Icons the client ships itself; there is no file of ours to resolve.
GAME_ICONS = "s2r://panorama/images/icons/"

# What the client's Panorama parser accepts anywhere in a layout.
ALLOWED_ELEMENTS = {"root", "styles", "include", "Panel", "Label", "Image", "Button"}

# Attributes known to pass the client's custom HUD validation; any other rejects the whole layout.
ALLOWED_ATTRIBUTES = {"id", "class", "hittest", "text", "src", "textureheight"}

# The client interns every id, variable and class name in one table shared by all screens, and
# overflowing it breaks rendering on players' machines, where no build step can see it.
NAME_TABLE_SIZE = 1024

# One screen past this many names is a runaway loop, not a design.
MAX_NAMES_PER_SCREEN = 400


def check_screens(root: Path, names: list[str] | None = None) -> list[CheckResult]:
    """Every problem the named plugins' screens would fail on in the client."""
    problems: list[str] = []
    claimed: dict[str, str] = {}
    names_by_screen: dict[Path, set[str]] = {}

    all_plugins = panorama_plugins(root)
    for plugin in panorama_plugins(root, names):
        renderer = ScreenRenderer(plugin, all_plugins)
        for icon_set, icons in renderer.icons.items():
            for icon in icons:
                resource = f"images/{icon_set}/{icon}.*"
                problems += _claim_resource(resource, plugin, claimed)
        for source in screen_templates(plugin):
            problems += _check_screen(renderer, source, claimed, names_by_screen)

    problems += _check_name_table(names_by_screen)
    return [CheckResult.fail(problem) for problem in problems]


def _check_screen(
    renderer: ScreenRenderer,
    source: Path,
    claimed: dict[str, str],
    names_by_screen: dict[Path, set[str]],
) -> list[str]:
    plugin, name = renderer.plugin, screen_name(source)
    problems = _claim_resource(f"layout/custom_game/{name}.xml", plugin, claimed)
    problems += _claim_resource(f"styles/custom_game/{name}.css", plugin, claimed)

    layout, stylesheet = renderer.render(name)
    try:
        screen = read_screen(layout, stylesheet, source)
    except VoltmodError as error:
        return problems + [str(error)]

    return (
        problems
        + _check_markup(screen, source)
        + _check_buttons(screen, source)
        + _check_ids(screen, name, source)
        + _check_cpp_names(screen, source)
        + _check_stylesheet_include(screen, name, source)
        + _check_images(plugin, screen, source)
        + _check_screen_names(screen, stylesheet, source, names_by_screen)
    )


def _check_markup(screen: Screen, source: Path) -> list[str]:
    problems: list[str] = []
    for node in screen.tree.iter():
        if node.tag not in ALLOWED_ELEMENTS:
            problems.append(f"{source}: <{node.tag}> is not an allowed element")
        for name in node.attrib:
            if name not in ALLOWED_ATTRIBUTES:
                problems.append(f"{source}: <{node.tag}> has disallowed attribute '{name}'")
    return problems


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


def _check_ids(screen: Screen, expected_name: str, source: Path) -> list[str]:
    """An id names the screen or starts with its name, and never repeats."""
    if not screen.name:
        return [f"{source}: no element carries an id, so there is nothing to name the screen"]

    problems: list[str] = []
    if screen.name != expected_name:
        problems.append(
            f"{source}: screen id '{screen.name}' does not match source name '{expected_name}'"
        )
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

    def take(spelled: str, origin: str) -> None:
        if spelled in taken:
            problems.append(f"{source}: {taken[spelled]} and {origin} both spell {spelled}")
        taken[spelled] = origin

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

    for prefix in screen.modifiers:
        if not is_cpp_name(prefix):
            problems.append(f"{source}: modifier class '{prefix}--*' cannot be spelled in C++")
        take(f"{pascal_case(prefix)}Classes", f"the {prefix}--* classes")
        take(f"{pascal_case(prefix)}Names", f"the {prefix}--* names")
    return problems


def _check_stylesheet_include(screen: Screen, name: str, source: Path) -> list[str]:
    expected = f"file://{{resources}}/styles/custom_game/{name}.css"
    found = [node.get("src", "") for node in screen.tree.iter("include")]
    if found == [expected]:
        return []
    got = ", ".join(found) or "none"
    return [f"{source}: expected one style include of '{expected}', got {got}"]


def _check_images(plugin: Plugin, screen: Screen, source: Path) -> list[str]:
    problems: list[str] = []
    for image in screen.tree.iter("Image"):
        src = image.get("src", "")
        if src.startswith(GAME_ICONS):
            continue
        match = IMAGE_SOURCE.match(src)
        if not match:
            problems.append(
                f"{source}: Image src '{src}' is neither "
                "s2r://panorama/images/<set>/<name>.vtex "
                "nor a game icon under s2r://panorama/images/icons/"
            )
        elif not icon_path(plugin, *match.groups()).is_file():
            icon_set, name = match.groups()
            problems.append(f"{source}: Image src '{src}' has no {icon_set}/{name}.png")
    return problems


def _check_screen_names(
    screen: Screen, stylesheet: str, source: Path, names_by_screen: dict[Path, set[str]]
) -> list[str]:
    """Record the names `screen` interns, and flag a screen that has run away on its own."""
    names = {screen.name, *screen.ids, *screen.variables, *selector_classes(stylesheet)}
    for node in screen.tree.iter():
        names.update(node.get("class", "").split())
    names_by_screen[source] = names

    if len(names) <= MAX_NAMES_PER_SCREEN:
        return []
    limit = MAX_NAMES_PER_SCREEN
    return [f"{source}: {len(names)} interned names, over the per-screen limit of {limit}"]


def _check_name_table(names_by_screen: dict[Path, set[str]]) -> list[str]:
    total = set().union(*names_by_screen.values())
    if len(total) <= NAME_TABLE_SIZE:
        return []
    largest = sorted(names_by_screen.items(), key=lambda pair: len(pair[1]), reverse=True)
    breakdown = ", ".join(f"{source} {len(names)}" for source, names in largest)
    return [
        f"{len(total)} interned names across all screens, "
        f"over the client's {NAME_TABLE_SIZE}: {breakdown}"
    ]


def _claim_resource(resource: str, plugin: Plugin, claimed: dict[str, str]) -> list[str]:
    """The first plugin to render `resource` keeps it; a second plugin is a problem."""
    holder = claimed.setdefault(resource, plugin.name)
    if holder == plugin.name:
        return []
    return [f"{resource}: rendered by both {holder} and {plugin.name}"]
