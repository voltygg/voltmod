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


@dataclass(slots=True)
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
        ids = ["Id" if suffix == "" else pascal(suffix) for suffix in self.id_suffixes]
        variables = [
            "Var" if suffix == "" else f"{pascal(suffix)}Var" for suffix in self.var_suffixes
        ]
        return ids + variables

    def ids(self, screen: str, index: int) -> list[str]:
        """The panel ids copy @p index carries, as the layout spells them."""
        return [
            f"{screen}_{self.stem}{index}" + (f"_{suffix}" if suffix else "")
            for suffix in self.id_suffixes
        ]

    def variables(self, index: int) -> list[str]:
        return [
            f"{self.stem}{index}" + (f"_{suffix}" if suffix else "") for suffix in self.var_suffixes
        ]


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
    _cached_groups: list[Group] | None = field(default=None, compare=False, repr=False)

    def groups(self) -> list[Group]:
        """The repeated blocks: every stem whose copies are indexed 0..K-1 (K >= 2) and alike."""
        if self._cached_groups is None:
            self._cached_groups = _groups(self)
        return self._cached_groups

    def grouped_ids(self) -> set[str]:
        """The panel ids the repeated blocks already spell, which no flat constant repeats."""
        return {
            name
            for group in self.groups()
            for i in range(group.count)
            for name in group.ids(self.name, i)
        }

    def grouped_variables(self) -> set[str]:
        """The dialog variables the repeated blocks already spell."""
        return {
            name
            for group in self.groups()
            for i in range(group.count)
            for name in group.variables(i)
        }


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

    groups = screen.groups()
    grouped_ids = screen.grouped_ids()
    grouped_vars = screen.grouped_variables()

    flat_ids = [name for name in screen.ids if name not in grouped_ids]
    if flat_ids:
        lines.append("// Panel ids.")
        for name in flat_ids:
            spelled = member(name, screen.name)
            lines.append(f'inline constexpr std::string_view {spelled} = "{name}";')
        lines.append("")

    flat_vars = [name for name in screen.variables if name not in grouped_vars]
    if flat_vars:
        lines.append("// Dialog variables, written through RootId.")
        for name in flat_vars:
            lines.append(f'inline constexpr std::string_view {pascal(name)}Var = "{name}";')
        lines.append("")

    if groups:
        lines.append(
            "// Repeated blocks: one struct per block, one array entry per index. Variables are"
        )
        lines.append("// written through RootId.")
        for group in groups:
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


def _groups(screen: Screen) -> list[Group]:
    """See `Screen.groups`. A stem whose copies differ, or skip an index, stays flat."""
    ids: dict[str, dict[int, list[str]]] = {}
    variables: dict[str, dict[int, list[str]]] = {}
    order: list[str] = []

    def add(table: dict[str, dict[int, list[str]]], name: str) -> None:
        found = INDEXED.match(name)
        if not found:
            return
        stem, index, suffix = found.group(1), int(found.group(2)), found.group(3) or ""
        if stem not in ids and stem not in variables:
            order.append(stem)
        table.setdefault(stem, {}).setdefault(index, []).append(suffix)

    for name in screen.ids:
        add(ids, name.removeprefix(f"{screen.name}_"))
    for name in screen.variables:
        add(variables, name)

    groups: list[Group] = []
    for stem in order:
        by_index_ids = ids.get(stem, {})
        by_index_vars = variables.get(stem, {})
        indices = sorted(set(by_index_ids) | set(by_index_vars))
        if len(indices) < 2 or indices != list(range(len(indices))):
            continue
        first_ids, first_vars = by_index_ids.get(0, []), by_index_vars.get(0, [])
        alike = all(
            sorted(by_index_ids.get(i, [])) == sorted(first_ids)
            and sorted(by_index_vars.get(i, [])) == sorted(first_vars)
            for i in indices
        )
        if alike:
            groups.append(Group(stem, len(indices), first_ids, first_vars))
    return groups


def _group(group: Group, screen: str) -> list[str]:
    """The struct and the array for one repeated block."""
    lines = [f"struct {group.struct}", "{"]
    lines += [f"    std::string_view {name};" for name in group.members()]
    lines += ["};", f"inline constexpr std::array<{group.struct}, {group.count}> {group.array}{{"]
    for index in range(group.count):
        values = [f'"{name}"' for name in group.ids(screen, index) + group.variables(index)]
        one = f"    {group.struct}{{{', '.join(values)}}},"
        if len(one) <= COLUMNS:
            lines.append(one)
        else:
            lines += (
                [f"    {group.struct}{{"] + [f"        {value}," for value in values] + ["    },"]
            )
    return lines + ["};", ""]


def _collect(families: dict[str, list[str]], classes: list[str]) -> None:
    """Add whatever `Prefix--variant` classes @p classes holds, keeping first-seen order."""
    for cls in classes:
        found = FAMILY.match(cls)
        if not found:
            continue
        variants = families.setdefault(found.group(1), [])
        if found.group(2) not in variants:
            variants.append(found.group(2))


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
    found: list[str] = []
    for selector, _ in rules(stylesheet):
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
