"""Read a rendered screen, and emit the C++ constants that name its parts.

A screen offers three things a plugin has to spell: the panel ids in its layout, the dialog
variables its `text="{s:var}"` Labels read, and the `Prefix--variant` class families its
stylesheet declares. This emits one constant for each and stops there. How those are assembled
into `Text`, `Flag` and `Choice` writers is the plugin's own code, where it reads plainly.

Nothing here validates or reports: `check.py` owns every rule a screen has to follow, and reads
the same `Screen` this returns so the two cannot disagree about what a layout says.
"""

import re
from dataclasses import dataclass, field
from xml.etree import ElementTree

#: A Label reading a dialog variable off the layout root.
VAR = re.compile(r"^\{s:(\w+)\}$")
#: One class of a `Prefix--variant` family.
FAMILY = re.compile(r"^([A-Za-z_]\w*)--([A-Za-z0-9_]+)$")
#: A class token inside a stylesheet selector.
CLASS = re.compile(r"\.([A-Za-z0-9_-]+)")
#: An Image pointing at a compiled icon.
IMAGE_SRC = re.compile(r"^s2r://panorama/images/custom_game/([^/]+)/([^/]+)\.vtex$")
#: The C++ namespace a template asks for, as `{# namespace: Some::Name #}`.
NAMESPACE = re.compile(r"\{#-?\s*namespace:\s*([A-Za-z_][A-Za-z0-9_:]*)\s*-?#\}")

_IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
_RULE = re.compile(r"([^{}]*)\{[^{}]*\}", re.DOTALL)
_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)

#: Wide enough for a constant per line; a longer array is broken up item per item.
COLUMNS = 100


@dataclass(slots=True)
class Screen:
    """What one rendered screen offers, in the order its layout and stylesheet name it."""

    #: The outermost panel id, which is also the layout resource name.
    name: str
    tree: ElementTree.Element
    #: Every other panel id, in document order.
    ids: list[str] = field(default_factory=list)
    #: Dialog variable names, in document order.
    variables: list[str] = field(default_factory=list)
    #: Family prefix -> its variants, both in first-appearance order.
    families: dict[str, list[str]] = field(default_factory=dict)


def read(layout: str, stylesheet: str) -> Screen:
    """Parse one rendered screen. Raises `ElementTree.ParseError` on a malformed layout."""
    tree = ElementTree.fromstring(layout)
    nodes = list(tree.iter())

    ids = [node.get("id", "") for node in nodes if node.get("id")]
    screen = Screen(name=ids[0] if ids else "", tree=tree, ids=ids[1:])

    for node in nodes:
        found = VAR.match(node.get("text", ""))
        if found and found.group(1) not in screen.variables:
            screen.variables.append(found.group(1))

    # The layout first, so an icon set's families keep the order its Images are stacked in; then
    # the stylesheet, which is the only place an Accent or Step family is ever declared.
    for node in nodes:
        _collect(screen.families, node.get("class", "").split())
    _collect(screen.families, _selector_classes(stylesheet))

    return screen


def header(screen: Screen, template: str) -> str:
    """The C++ header for @p screen. @p template is its source, read for a namespace directive."""
    found = NAMESPACE.search(template)
    namespace = found.group(1) if found else f"Screens::{pascal(screen.name)}"

    lines = [
        "#pragma once",
        "",
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

    if screen.ids:
        lines.append("// Panel ids.")
        for name in screen.ids:
            spelled = member(name, screen.name)
            lines.append(f'inline constexpr std::string_view {spelled} = "{name}";')
        lines.append("")

    if screen.variables:
        lines.append("// Dialog variables, written through RootId.")
        for name in screen.variables:
            lines.append(f'inline constexpr std::string_view {pascal(name)}Var = "{name}";')
        lines.append("")

    for prefix, variants in screen.families.items():
        lines += _family(prefix, variants)

    return "\n".join(lines + [f"}}  // namespace {namespace}", ""])


def pascal(name: str) -> str:
    """`voltmod_menu` -> `VoltmodMenu`, the name CMake spells for the same screen."""
    return "".join(word[:1].upper() + word[1:] for word in name.split("_") if word)


def member(identifier: str, screen: str) -> str:
    """`cs2_hud_card0_bar` under screen `cs2_hud` -> `Card0Bar`."""
    return pascal(identifier.removeprefix(f"{screen}_"))


def spellable(name: str) -> bool:
    """Whether @p name can be a C++ member name once pascal-cased."""
    return bool(_IDENTIFIER.match(pascal(name)))


def enumerated(variants: list[str]) -> bool:
    """Whether a family gets an `enum class`. Step numbers do not; named variants do."""
    return not all(variant.isdigit() for variant in variants)


def _collect(families: dict[str, list[str]], classes: list[str]) -> None:
    """Add whatever `Prefix--variant` classes @p classes holds, keeping first-seen order."""
    for cls in classes:
        found = FAMILY.match(cls)
        if not found:
            continue
        variants = families.setdefault(found.group(1), [])
        if found.group(2) not in variants:
            variants.append(found.group(2))


def _selector_classes(stylesheet: str) -> list[str]:
    """Every class named by a selector, in rule order. Declarations are skipped so a decimal in
    a value cannot be read as a class."""
    found: list[str] = []
    for selector in _RULE.findall(_COMMENT.sub(" ", stylesheet)):
        found += CLASS.findall(selector)
    return found


def _family(prefix: str, variants: list[str]) -> list[str]:
    """A family's classes, plus the enum naming them unless they are step numbers."""
    lines: list[str] = []
    if enumerated(variants):
        lines += [f"enum class {prefix}", "{"]
        lines += [f"    {pascal(variant)}," for variant in variants]
        lines += ["};"]
        lines += _array(f"{prefix}Names", [f'"{variant}"' for variant in variants])

    classes = _array(f"{prefix}Classes", [f'"{prefix}--{variant}"' for variant in variants])
    return lines + classes + [""]


def _array(name: str, items: list[str]) -> list[str]:
    """One line when it fits the column limit, otherwise one item per line."""
    head = f"inline constexpr std::array<std::string_view, {len(items)}> {name}"
    one = f"{head}{{{', '.join(items)}}};"
    if len(one) <= COLUMNS:
        return [one]
    return [f"{head}{{"] + [f"    {item}," for item in items] + ["};"]
