"""Reading a rendered screen's panel ids, dialog variables, class modifiers and repeated blocks."""

import re
from dataclasses import dataclass, field
from functools import cached_property
from pathlib import Path
from xml.etree import ElementTree

from voltmod.errors import VoltmodError

# A Label reading a dialog variable off the layout root.
DIALOG_VARIABLE = re.compile(r"^\{s:(\w+)\}$")

# One class of a BEM `block--modifier` or `block__element--modifier` set.
MODIFIER_CLASS = re.compile(r"^([A-Za-z_]\w*(?:-\w+)*)--(\w+(?:-\w+)*)$")
SELECTOR_CLASS = re.compile(r"\.([A-Za-z0-9_-]+)")
IMAGE_SOURCE = re.compile(r"^s2r://panorama/images/([^/]+)/([^/]+)\.vtex$")

# The C++ namespace a template asks for, as `{# namespace: Some::Name #}`.
NAMESPACE_DIRECTIVE = re.compile(r"\{#-?\s*namespace:\s*([A-Za-z_][A-Za-z0-9_:]*)\s*-?#\}")

# A name inside a repeated block: `<name><index>`, with an optional `_<suffix>`.
INDEXED_NAME = re.compile(r"^([a-z][a-z_]*?)(\d+)(?:_(\w+))?$")

_CPP_IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
_CSS_RULE = re.compile(r"([^{}]*)\{([^{}]*)\}", re.DOTALL)
_CSS_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
# A statement at-rule such as `@define accent: #e1273c;`, which is not part of the next selector.
_CSS_AT_STATEMENT = re.compile(r"@[\w-]+[^;{}]*;")


@dataclass(frozen=True, slots=True)
class RepeatedBlock:
    """A block the layout repeats with an index, such as `row0`..`row7`, alike in every copy.

    The header spells it as `struct Row` plus `std::array<Row, 8> Rows`.
    """

    name: str
    count: int
    # Document order; "" is the block's own panel.
    id_suffixes: list[str]
    # Document order; "" is a bare `<name><index>` variable.
    variable_suffixes: list[str]

    @property
    def struct_name(self) -> str:
        return pascal_case(self.name)

    @property
    def array_name(self) -> str:
        return f"{self.struct_name}s"

    def members(self) -> list[str]:
        """The struct's member names, ids first, in the order the layout names them."""
        ids = [pascal_case(suffix) or "Id" for suffix in self.id_suffixes]
        return ids + [f"{pascal_case(suffix)}Var" for suffix in self.variable_suffixes]

    def ids(self, screen: str, index: int) -> list[str]:
        return [_with_suffix(f"{screen}_{self.name}{index}", suffix) for suffix in self.id_suffixes]

    def variables(self, index: int) -> list[str]:
        return [_with_suffix(f"{self.name}{index}", suffix) for suffix in self.variable_suffixes]


@dataclass
class Screen:
    """What one rendered screen offers, in the order its layout and stylesheet name it."""

    # The outermost panel id, which is also the layout resource name.
    name: str
    tree: ElementTree.Element
    ids: list[str] = field(default_factory=list)
    variables: list[str] = field(default_factory=list)
    # `block` or `block__element` -> its modifiers, both in first-appearance order.
    modifiers: dict[str, list[str]] = field(default_factory=dict)

    @cached_property
    def blocks(self) -> list[RepeatedBlock]:
        return _find_blocks(self)

    @cached_property
    def block_ids(self) -> set[str]:
        return {
            name
            for block in self.blocks
            for index in range(block.count)
            for name in block.ids(self.name, index)
        }

    @cached_property
    def block_variables(self) -> set[str]:
        return {
            name
            for block in self.blocks
            for index in range(block.count)
            for name in block.variables(index)
        }

    @property
    def flat_ids(self) -> list[str]:
        return [name for name in self.ids if name not in self.block_ids]

    @property
    def flat_variables(self) -> list[str]:
        return [name for name in self.variables if name not in self.block_variables]


def read_screen(layout: str, stylesheet: str, source: Path | None = None) -> Screen:
    """Parse one rendered screen; a malformed layout raises a VoltmodError naming `source`."""
    try:
        tree = ElementTree.fromstring(layout)
    except ElementTree.ParseError as error:
        where = f"{source}: " if source else ""
        raise VoltmodError(f"{where}the rendered layout is not well-formed XML: {error}") from None
    nodes = list(tree.iter())

    ids = [node.get("id", "") for node in nodes if node.get("id")]
    screen = Screen(name=ids[0] if ids else "", tree=tree, ids=ids[1:])

    # Layout first, so an icon set's modifiers keep the order its Images are stacked in.
    for node in nodes:
        found = DIALOG_VARIABLE.match(node.get("text", ""))
        if found and found.group(1) not in screen.variables:
            screen.variables.append(found.group(1))
        _collect_modifiers(screen.modifiers, node.get("class", "").split())
    _collect_modifiers(screen.modifiers, selector_classes(stylesheet))
    return screen


def header_namespace(screen: Screen, template_source: str) -> str:
    """The namespace the template names, or `Screens::<Screen>`."""
    found = NAMESPACE_DIRECTIVE.search(template_source)
    return found.group(1) if found else f"Screens::{pascal_case(screen.name)}"


def pascal_case(name: str) -> str:
    """`voltmod_menu` or `icon-set` -> `VoltmodMenu` or `IconSet`; CMake spells screens the same."""
    return "".join(word[:1].upper() + word[1:] for word in re.split(r"[_-]", name) if word)


def member_name(identifier: str, screen: str) -> str:
    """`cs2_hud_card0_bar` on screen `cs2_hud` -> `Card0Bar`."""
    return pascal_case(identifier.removeprefix(f"{screen}_"))


def is_cpp_name(name: str) -> bool:
    return bool(_CPP_IDENTIFIER.match(pascal_case(name)))


def css_rules(stylesheet: str) -> list[tuple[str, str]]:
    """Every rule as (selector, declarations), in source order, comments and `@define`s stripped."""
    text = _CSS_AT_STATEMENT.sub(" ", _CSS_COMMENT.sub(" ", stylesheet))
    return [
        (selector.strip(), body) for selector, body in _CSS_RULE.findall(text) if selector.strip()
    ]


def selector_classes(stylesheet: str) -> list[str]:
    """Every class a selector names; declarations are skipped so a decimal is never a class."""
    return [
        name for selector, _ in css_rules(stylesheet) for name in SELECTOR_CLASS.findall(selector)
    ]


def _with_suffix(base: str, suffix: str) -> str:
    return f"{base}_{suffix}" if suffix else base


def _find_blocks(screen: Screen) -> list[RepeatedBlock]:
    """Names indexed 0..N-1 (N >= 2) whose copies are alike; a gap or a difference stays flat."""
    # name -> index -> (id suffixes, variable suffixes), names in first-appearance order.
    copies: dict[str, dict[int, tuple[list[str], list[str]]]] = {}

    def add(name: str, slot: int) -> None:
        if found := INDEXED_NAME.match(name):
            block, index, suffix = found.group(1), int(found.group(2)), found.group(3) or ""
            copies.setdefault(block, {}).setdefault(index, ([], []))[slot].append(suffix)

    for identifier in screen.ids:
        add(identifier.removeprefix(f"{screen.name}_"), 0)
    for variable in screen.variables:
        add(variable, 1)

    blocks = []
    for name, by_index in copies.items():
        if len(by_index) < 2 or sorted(by_index) != list(range(len(by_index))):
            continue
        first_ids, first_variables = by_index[0]
        alike = all(
            sorted(ids) == sorted(first_ids) and sorted(variables) == sorted(first_variables)
            for ids, variables in by_index.values()
        )
        if alike:
            blocks.append(RepeatedBlock(name, len(by_index), first_ids, first_variables))
    return blocks


def _collect_modifiers(modifiers: dict[str, list[str]], classes: list[str]) -> None:
    for name in classes:
        if found := MODIFIER_CLASS.match(name):
            known = modifiers.setdefault(found.group(1), [])
            if found.group(2) not in known:
                known.append(found.group(2))
