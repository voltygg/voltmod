"""Read a rendered screen, and emit the C++ constants that name its parts.

A screen offers three things a plugin has to spell: the panel ids in its layout, the dialog
variables its `text="{s:var}"` Labels read, and the `Prefix--variant` class families its
stylesheet declares. This emits one constant for each. A block a template repeats - `row0` to
`row7`, each with the same children and variables - is emitted once as a struct plus an array,
so the plugin indexes it instead of spelling every copy. How those constants are assembled into
`TextVar`, `ClassFlag` and `ClassChoice` writers is the plugin's own code, where it reads plainly.

Nothing here validates or reports: `check.py` owns every rule a screen has to follow, and reads
the same `Screen` this returns so the two cannot disagree about what a layout says.
"""

import re
from dataclasses import dataclass, field
from functools import cached_property
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
#: A name that belongs to a repeated block: `<stem><index>` with an optional `_<suffix>`.
INDEXED = re.compile(r"^([a-z][a-z_]*?)(\d+)(?:_(\w+))?$")

_IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
_RULE = re.compile(r"([^{}]*)\{([^{}]*)\}", re.DOTALL)
_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)

#: Wide enough for a constant per line; a longer array is broken up item per item.
COLUMNS = 100


def _suffixed(base: str, suffix: str) -> str:
    return f"{base}_{suffix}" if suffix else base


@dataclass(frozen=True, slots=True)
class Group:
    """A block the layout repeats with an index: `row0`..`row7`, alike in every copy.

    Emitted as `struct <Stem>` with one `std::string_view` per member and `std::array<<Stem>, count>
    <Stem>s`. The array is the struct name plus `s`; a stem that already ends in `s` should be
    renamed in the template rather than pluralised here.
    """

    #: `row` for `row0`..`row7`.
    stem: str
    count: int
    #: Id suffixes in document order; `""` is the block's own panel.
    id_suffixes: list[str]
    #: Dialog-variable suffixes in document order; `""` is a bare `<stem><index>` variable.
    var_suffixes: list[str]

    @property
    def struct(self) -> str:
        return pascal(self.stem)

    @property
    def array(self) -> str:
        return f"{self.struct}s"

    def members(self) -> list[str]:
        """The struct's member names, ids first, in the order the layout names them."""
        ids = [pascal(suffix) or "Id" for suffix in self.id_suffixes]
        variables = [f"{pascal(suffix)}Var" for suffix in self.var_suffixes]
        return ids + variables

    def ids(self, screen: str, index: int) -> list[str]:
        """The panel ids copy @p index carries, as the layout spells them."""
        return [_suffixed(f"{screen}_{self.stem}{index}", suffix) for suffix in self.id_suffixes]

    def variables(self, index: int) -> list[str]:
        return [_suffixed(f"{self.stem}{index}", suffix) for suffix in self.var_suffixes]


@dataclass
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

    @cached_property
    def groups(self) -> list[Group]:
        """The repeated blocks: every stem whose copies are indexed 0..K-1 (K >= 2) and alike."""
        return _groups(self)

    @cached_property
    def grouped_ids(self) -> set[str]:
        """The panel ids the repeated blocks already spell, which no flat constant repeats."""
        return {n for g in self.groups for i in range(g.count) for n in g.ids(self.name, i)}

    @cached_property
    def grouped_variables(self) -> set[str]:
        """The dialog variables the repeated blocks already spell."""
        return {n for g in self.groups for i in range(g.count) for n in g.variables(i)}


def read(layout: str, stylesheet: str) -> Screen:
    """Parse one rendered screen. Raises `ElementTree.ParseError` on a malformed layout."""
    tree = ElementTree.fromstring(layout)
    nodes = list(tree.iter())

    ids = [node.get("id", "") for node in nodes if node.get("id")]
    screen = Screen(name=ids[0] if ids else "", tree=tree, ids=ids[1:])

    # The layout before the stylesheet, so an icon set's families keep the order its Images are
    # stacked in; the stylesheet is the only place an Accent or Step family is ever declared.
    for node in nodes:
        found = VAR.match(node.get("text", ""))
        if found and found.group(1) not in screen.variables:
            screen.variables.append(found.group(1))
        _collect(screen.families, node.get("class", "").split())
    _collect(screen.families, selector_classes(stylesheet))

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

    flat_ids = [name for name in screen.ids if name not in screen.grouped_ids]
    if flat_ids:
        lines.append("// Panel ids.")
        for name in flat_ids:
            spelled = member(name, screen.name)
            lines.append(f'inline constexpr std::string_view {spelled} = "{name}";')
        lines.append("")

    flat_vars = [name for name in screen.variables if name not in screen.grouped_variables]
    if flat_vars:
        lines.append("// Dialog variables, written through RootId.")
        for name in flat_vars:
            lines.append(f'inline constexpr std::string_view {pascal(name)}Var = "{name}";')
        lines.append("")

    if screen.groups:
        lines += [
            "// Repeated blocks: one struct per block, one array entry per index. Variables are",
            "// written through RootId.",
        ]
        for group in screen.groups:
            lines += _group(group, screen.name)

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


def rules(stylesheet: str) -> list[tuple[str, str]]:
    """Every rule as (selector, declarations), in source order, comments stripped.

    The one place a stylesheet is tokenized, so what binds a screen and what previews it cannot
    read the same file differently.
    """
    return [
        (selector.strip(), body)
        for selector, body in _RULE.findall(_COMMENT.sub(" ", stylesheet))
        if selector.strip()
    ]


def selector_classes(stylesheet: str) -> list[str]:
    """Every class named by a selector, in rule order. Declarations are skipped so a decimal in
    a value cannot be read as a class."""
    return [cls for selector, _ in rules(stylesheet) for cls in CLASS.findall(selector)]


def _groups(screen: Screen) -> list[Group]:
    """See `Screen.groups`. A stem whose copies differ, or skip an index, stays flat."""
    # stem -> index -> (id suffixes, variable suffixes), stems in first-appearance order.
    copies: dict[str, dict[int, tuple[list[str], list[str]]]] = {}

    def add(name: str, slot: int) -> None:
        if found := INDEXED.match(name):
            stem, index, suffix = found.group(1), int(found.group(2)), found.group(3) or ""
            copies.setdefault(stem, {}).setdefault(index, ([], []))[slot].append(suffix)

    for name in screen.ids:
        add(name.removeprefix(f"{screen.name}_"), 0)
    for name in screen.variables:
        add(name, 1)

    groups: list[Group] = []
    for stem, by_index in copies.items():
        if len(by_index) < 2 or sorted(by_index) != list(range(len(by_index))):
            continue
        first_ids, first_vars = by_index[0]
        alike = all(
            sorted(ids) == sorted(first_ids) and sorted(variables) == sorted(first_vars)
            for ids, variables in by_index.values()
        )
        if alike:
            groups.append(Group(stem, len(by_index), first_ids, first_vars))
    return groups


def _group(group: Group, screen: str) -> list[str]:
    """The struct and the array for one repeated block."""
    lines = [f"struct {group.struct}", "{"]
    lines += [f"    std::string_view {name};" for name in group.members()]
    lines += ["};", f"inline constexpr std::array<{group.struct}, {group.count}> {group.array}{{"]
    for index in range(group.count):
        values = [f'"{name}"' for name in group.ids(screen, index) + group.variables(index)]
        lines += _braced(group.struct, values, ",", indent="    ")
    return lines + ["};", ""]


def _collect(families: dict[str, list[str]], classes: list[str]) -> None:
    """Add whatever `Prefix--variant` classes @p classes holds, keeping first-seen order."""
    for cls in classes:
        if found := FAMILY.match(cls):
            variants = families.setdefault(found.group(1), [])
            if found.group(2) not in variants:
                variants.append(found.group(2))


def _family(prefix: str, variants: list[str]) -> list[str]:
    """A family's classes, plus the enum naming them unless they are step numbers."""
    lines: list[str] = []
    if enumerated(variants):
        lines += [f"enum class {prefix}", "{"]
        lines += [f"    {pascal(variant)}," for variant in variants]
        lines += ["};"]
        lines += _array(f"{prefix}Names", [f'"{variant}"' for variant in variants])
    return lines + _array(f"{prefix}Classes", [f'"{prefix}--{v}"' for v in variants]) + [""]


def _array(name: str, items: list[str]) -> list[str]:
    head = f"inline constexpr std::array<std::string_view, {len(items)}> {name}"
    return _braced(head, items, ";")


def _braced(head: str, items: list[str], tail: str, indent: str = "") -> list[str]:
    """`head{items}tail` on one line when it fits the column limit, else one item per line."""
    one = f"{indent}{head}{{{', '.join(items)}}}{tail}"
    if len(one) <= COLUMNS:
        return [one]
    items = [f"{indent}    {item}," for item in items]
    return [f"{indent}{head}{{", *items, f"{indent}}}{tail}"]
