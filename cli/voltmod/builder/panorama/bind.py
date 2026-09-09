"""Derive a screen's C++ binding from its rendered layout and stylesheet.

The layout says what exists and the stylesheet says what may change: an id names a panel, a
`text="{s:var}"` label names a dialog variable, and a class the stylesheet only ever pairs with a
class the panel already carries is a state the server writes. `docs/custom-ui.md` spells the rules
out; this turns them into one header of `Text`, `Flag` and `OneOf` constants.
"""

import re
from dataclasses import dataclass, field, replace
from xml.etree import ElementTree

from voltmod.tools import die

#: Names the header defines for itself, so no derived member may take one.
RESERVED = ("Layout", "RootId")

#: Wide enough for a struct member per line; anything longer is broken up item per item.
COLUMNS = 100

_VAR = re.compile(r"^\{s:(\w+)\}$")
_FAMILY = re.compile(r"^(\w+)--(\w+)$")
_SEGMENT = re.compile(r"^(.*?)(\d*)$")
_IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
_CLASS = re.compile(r"\.([A-Za-z0-9_-]+)")
_RULE = re.compile(r"([^{}]*)\{[^{}]*\}", re.DOTALL)
_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_NAMESPACE = re.compile(r"\{#-?\s*namespace:\s*([A-Za-z_][A-Za-z0-9_:]*)\s*-?#\}")


@dataclass(frozen=True, slots=True)
class Widget:
    """One writer: the panel path it belongs to, its C++ type, and how it is initialised.

    @p nested is the member name it takes when its panel holds more than one writer; alone on a
    panel it takes that panel's own leaf name instead.
    """

    path: tuple[str, ...]
    nested: str
    type: str
    init: str
    family: str = ""


@dataclass(slots=True)
class Slot:
    """One member of a struct: a writer, a nested struct, or either of those indexed."""

    leaf: Widget | None = None
    members: dict[str, Slot] | None = None
    items: dict[int, Slot] | None = None
    type: str = ""


@dataclass(slots=True)
class Screen:
    """What one rendered screen offers: its root id, its writers, and its class families."""

    name: str
    widgets: list[Widget] = field(default_factory=list)
    families: dict[str, list[str]] = field(default_factory=dict)


def header(layout: str, stylesheet: str, template: str, source: str) -> str:
    """The C++ binding header for one rendered screen."""
    screen = _read(layout, stylesheet, source)
    tree = _tree(screen, source)
    _verify(tree, screen, source)

    found = _NAMESPACE.search(template)
    namespace = found.group(1) if found else f"Screens::{pascal(screen.name)}"
    return _emit(screen, tree, namespace)


def pascal(name: str) -> str:
    """`voltmod_menu` -> `VoltmodMenu`, the name CMake spells for the same screen."""
    return "".join(word[:1].upper() + word[1:] for word in name.split("_") if word)


def _read(layout: str, stylesheet: str, source: str) -> Screen:
    """Everything the layout and its stylesheet say, before any of it is placed."""
    nodes = list(_parse(layout, source).iter())
    screen = Screen(_screen_name(nodes, source))
    compounds = _compounds(stylesheet)
    variables: set[str] = set()
    checked: list[tuple[str, list[str]]] = []

    for node in nodes:
        # A dialog variable is named by the label that reads it, whether or not it has an id.
        variable = _VAR.match(node.get("text", ""))
        if variable and variable.group(1) not in variables:
            name = variable.group(1)
            variables.add(name)
            widget = Widget(_path(name, source), "Text", "VoltMod::Text", f'RootId, "{name}"')
            screen.widgets.append(widget)

        identifier = node.get("id", "")
        if not identifier:
            continue
        under = identifier[len(screen.name) + 1 :]
        path = _path(under, source) if under else ()

        if node.tag == "Button":
            screen.widgets.append(Widget(path, "Id", "std::string_view", f'"{identifier}"'))

        states = _states(node, compounds) + _child_variants(node)
        checked.append((identifier, states))
        _panel(screen, identifier, path, states, source)

        if not path and "Hidden" in node.get("class", "").split():
            screen.widgets.append(Widget((), "Hidden", "VoltMod::Flag", 'RootId, "Hidden"'))

    _verify_families(screen, checked, source)
    return _sized(screen)


def _panel(
    screen: Screen, identifier: str, path: tuple[str, ...], states: list[str], source: str
) -> None:
    """The states one panel can be put into: a flag each, or a `Prefix--variant` family."""
    panel = f'"{identifier}"' if path else "RootId"
    families: set[str] = set()
    for state in states:
        found = _FAMILY.match(state)
        if not found:
            screen.widgets.append(
                Widget(path, _name(state, source), "VoltMod::Flag", f'{panel}, "{state}"')
            )
            continue

        prefix, variant = found.group(1), found.group(2)
        variants = screen.families.setdefault(_name(prefix, source), [])
        if variant not in variants:
            variants.append(variant)
        if prefix not in families:
            families.add(prefix)
            screen.widgets.append(
                Widget(path, prefix, "", f"{panel}, {prefix}Classes", family=prefix)
            )


def _sized(screen: Screen) -> Screen:
    """A family's `OneOf<N>` is only known once every element has been read."""
    screen.widgets = [
        widget
        if not widget.family
        else replace(widget, type=f"VoltMod::OneOf<{len(screen.families[widget.family])}>")
        for widget in screen.widgets
    ]
    return screen


def _parse(layout: str, source: str) -> ElementTree.Element:
    try:
        return ElementTree.fromstring(layout)
    except ElementTree.ParseError as error:
        die(f"{source}: the rendered layout is not well-formed XML: {error}")


def _screen_name(nodes: list[ElementTree.Element], source: str) -> str:
    """The screen's name: the outermost id. Every other id has to sit under it."""
    found = [node.get("id", "") for node in nodes if node.get("id")]
    if not found:
        die(f"{source}: no element carries an id, so there is nothing to name the screen")

    screen = found[0]
    for identifier in found[1:]:
        if not identifier.startswith(f"{screen}_"):
            die(f"{source}: id '{identifier}' does not start with '{screen}_'")
    return screen


def _compounds(stylesheet: str) -> list[list[str]]:
    """Every selector compound in the stylesheet, in rule order, as its class names.

    A compound is one `.a.b` group: what the stylesheet wants on the same panel at the same time.
    """
    found: list[list[str]] = []
    for selector in _RULE.findall(_COMMENT.sub(" ", stylesheet)):
        for one in selector.split(","):
            for compound in re.split(r"[\s>]+", one.strip()):
                classes = _CLASS.findall(compound)
                if classes:
                    found.append(classes)
    return found


def _states(node: ElementTree.Element, compounds: list[list[str]]) -> list[str]:
    """The classes the stylesheet pairs with the panel's own class, in rule order.

    The first class is the panel's own; the rest of what it carries is already true of it. So
    `.Row.Selected` makes Selected a state of a `class="Row Hidden"` panel, while `.Nav-tab.Hidden`
    - which shares only the vocabulary class - says nothing about that panel at all.
    """
    static = node.get("class", "").split()
    found: list[str] = []
    for compound in compounds:
        if not static or static[0] not in compound:
            continue
        for cls in compound:
            if cls not in static and cls not in found:
                found.append(cls)
    return found


def _child_variants(node: ElementTree.Element) -> list[str]:
    """A family every direct child belongs to: how an icon set names its icons.

    Every child has to carry one, so a lone `Nav--narrow` button among plain ones is not a family.
    """
    families: dict[str, list[str]] = {}
    for child in node:
        for cls in child.get("class", "").split():
            found = _FAMILY.match(cls)
            if found and cls not in families.setdefault(found.group(1), []):
                families[found.group(1)].append(cls)

    children = len(list(node))
    return [cls for classes in families.values() if len(classes) == children for cls in classes]


def _verify_families(screen: Screen, checked: list[tuple[str, list[str]]], source: str) -> None:
    """Two panels may share a family, but not disagree about what is in it."""
    for identifier, states in checked:
        mine: dict[str, list[str]] = {}
        for state in states:
            found = _FAMILY.match(state)
            if found:
                mine.setdefault(found.group(1), []).append(found.group(2))

        for prefix, variants in mine.items():
            known = screen.families[prefix]
            if variants != known:
                die(
                    f"{source}: '{identifier}' has {prefix}--[{', '.join(variants)}] where "
                    f"another panel has {prefix}--[{', '.join(known)}]"
                )


def _path(text: str, source: str) -> tuple[str, ...]:
    """`card0_bar` -> ('card0', 'bar'): the segments that place a member."""
    segments = tuple(segment for segment in text.split("_") if segment)
    if not segments:
        die(f"{source}: '{text}' names nothing to place a member by")
    return segments


def _name(text: str, source: str) -> str:
    """A class name as a C++ member name, or why it cannot be one."""
    name = pascal(text)
    if not _IDENTIFIER.match(name):
        die(f"{source}: class '{text}' is not a name a C++ member can take")
    return name


def _member(segment: str, source: str) -> tuple[str, int | None]:
    """`card0` -> ('Card', 0): a trailing run of digits is an array index."""
    text, digits = _SEGMENT.match(segment).groups()
    return _name(text, source), int(digits) if digits else None


def _tree(screen: Screen, source: str) -> dict[str, Slot]:
    """Place every writer, collapsing a panel that has only one onto the panel's own name."""
    root: dict[str, Slot] = {}
    for widget in screen.widgets:
        alone = sum(1 for other in screen.widgets if other.path == widget.path) == 1
        deeper = any(
            other.path[: len(widget.path)] == widget.path and len(other.path) > len(widget.path)
            for other in screen.widgets
        )
        members = [_member(segment, source) for segment in widget.path]
        if not members or not alone or deeper:
            members.append((widget.nested, None))
        _place(root, members, widget, source)
    return root


def _place(
    node: dict[str, Slot], members: list[tuple[str, int | None]], widget: Widget, source: str
) -> None:
    name, index = members[0]
    slot = node.setdefault(name, Slot())
    if index is None:
        if slot.items is not None:
            die(f"{source}: {name} is written both as a value and as an array")
        target = slot
    else:
        if slot.leaf or slot.members:
            die(f"{source}: {name} is written both as a value and as an array")
        if slot.items is None:
            slot.items = {}
        target = slot.items.setdefault(index, Slot())

    if len(members) == 1:
        if target.leaf or target.members:
            die(f"{source}: two writers want the member {_spell(members)}")
        target.leaf = widget
        return

    if target.leaf:
        die(f"{source}: two writers want the member {_spell(members[:1])}")
    if target.members is None:
        target.members = {}
    _place(target.members, members[1:], widget, source)


def _spell(members: list[tuple[str, int | None]]) -> str:
    return "".join(name if index is None else f"{name}[{index}]" for name, index in members)


def _verify(tree: dict[str, Slot], screen: Screen, source: str) -> None:
    """Every array is one shape with no gaps, and nothing collides at namespace level."""
    if not tree:
        die(f"{source}: nothing to bind - no ids, no {{s:var}} labels, no state classes")

    taken = set(RESERVED)
    for prefix in screen.families:
        taken.update((prefix, f"{prefix}Names", f"{prefix}Classes"))
    for name in tree:
        if name in taken:
            die(f"{source}: {name} names both a screen member and an id or class family")

    for name, slot in tree.items():
        _verify_slot(name, slot, source)


def _verify_slot(name: str, slot: Slot, source: str) -> None:
    if slot.members:
        for member, nested in slot.members.items():
            _verify_slot(f"{name}.{member}", nested, source)
    if slot.items is None:
        return

    missing = [index for index in range(max(slot.items) + 1) if index not in slot.items]
    if missing:
        die(f"{source}: {name} has no {', '.join(f'{name}{index}' for index in missing)}")

    shapes = {_shape(item) for item in slot.items.values()}
    if len(shapes) != 1:
        die(f"{source}: the entries of {name} are not all written the same way")
    for index, item in slot.items.items():
        _verify_slot(f"{name}[{index}]", item, source)


def _shape(slot: Slot) -> tuple:
    """What a slot looks like, so two array entries can be compared."""
    if slot.leaf:
        return ("writer", slot.leaf.type)
    if slot.items is not None:
        return ("array", len(slot.items), _shape(slot.items[min(slot.items)]))
    return ("struct", tuple((name, _shape(member)) for name, member in slot.members.items()))


def _emit(screen: Screen, tree: dict[str, Slot], namespace: str) -> str:
    """The header: the screen's ids, its families, the struct types, then the writers."""
    lines = [
        f"// Rendered by `voltmod panorama render` from panorama/screens/{screen.name}.xml.j2. "
        "Do not edit.",
        "#pragma once",
        "",
        "#include <VoltMod/Ui/Widgets.hpp>",
        "#include <array>",
        "#include <string_view>",
        "",
        f"namespace {namespace}",
        "{",
        "",
        f'inline constexpr std::string_view Layout = "{screen.name}";',
        f'inline constexpr std::string_view RootId = "{screen.name}";',
        "",
    ]

    for prefix, variants in screen.families.items():
        lines += _family(prefix, variants)

    definitions: list[str] = []
    for name, slot in tree.items():
        _resolve(name, slot, definitions)
    lines += definitions

    for name, slot in tree.items():
        lines += [f"inline constexpr {slot.type} {name}{_value(slot, 0)};", ""]

    return "\n".join(lines + [f"}}  // namespace {namespace}", ""])


def _family(prefix: str, variants: list[str]) -> list[str]:
    """A family's classes, plus the enum naming them unless they are step numbers."""
    lines: list[str] = []
    if not all(variant.isdigit() for variant in variants):
        lines += [f"enum class {prefix}", "{"]
        lines += [f"    {pascal(variant)}," for variant in variants]
        lines += ["};"]
        lines += _array(f"{prefix}Names", [f'"{variant}"' for variant in variants])

    classes = [f'"{prefix}--{variant}"' for variant in variants]
    return lines + _array(f"{prefix}Classes", classes) + [""]


def _array(name: str, items: list[str]) -> list[str]:
    """One line when it fits the column limit, otherwise one item per line."""
    head = f"inline constexpr std::array<std::string_view, {len(items)}> {name}"
    one = f"{head}{{{', '.join(items)}}};"
    if len(one) <= COLUMNS:
        return [one]
    return [f"{head}{{"] + [f"    {item}," for item in items] + ["};"]


def _resolve(name: str, slot: Slot, out: list[str], prefix: str = "", suffix: str = "Panel") -> str:
    """Define the struct types @p slot needs, children first, and answer its own C++ type."""
    if slot.items is not None:
        item = _resolve(name, slot.items[min(slot.items)], out, prefix, "Item")
        slot.type = f"std::array<{item}, {len(slot.items)}>"
    elif slot.members is not None:
        slot.type = _struct(f"{prefix}{name}", slot.members, out, suffix)
    else:
        slot.type = slot.leaf.type
    return slot.type


def _struct(path: str, members: dict[str, Slot], out: list[str], suffix: str) -> str:
    name = f"{path}{suffix}"
    body = [f"struct {name}", "{"]
    for member, slot in members.items():
        body.append(f"    {_resolve(member, slot, out, path)} {member};")
    out.extend(body + ["};", ""])
    return name


def _value(slot: Slot, depth: int) -> str:
    """A braced initialiser, one member per line so no line needs wrapping."""
    pad = "    " * (depth + 1)
    if slot.leaf:
        return f"{{{slot.leaf.init}}}"
    if slot.items is not None:
        rows = [f"{pad}{_value(slot.items[index], depth + 1)}," for index in sorted(slot.items)]
        return "{{\n" + "\n".join(rows) + "\n" + "    " * depth + "}}"
    rows = [f"{pad}{_value(member, depth + 1)}," for member in slot.members.values()]
    return "{\n" + "\n".join(rows) + "\n" + "    " * depth + "}"
