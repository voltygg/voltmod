from dataclasses import dataclass, field
from enum import StrEnum

# A class inheriting from this replicates writes through the entity itself.
ENTITY_BASE = "CEntityInstance"

# The engine's link from a replicated component to the entity that owns it.
OWNER_LINK_FIELD = "__m_pChainEntity"

# Stripped from a field name only when an uppercase letter follows, so `m_lifeState` stays whole.
# fmt: off
HUNGARIAN_PREFIXES = {
    "a", "ang", "arr", "b", "cl", "clr", "d", "e", "f", "fl", "fn", "h", "i", "isz", "m",
    "n", "nv", "p", "pp", "psz", "pv", "q", "s", "str", "sz", "t", "u", "ub", "ui", "un",
    "v", "vec", "w", "x", "y", "z",
}
# fmt: on


class FieldKind(StrEnum):
    VALUE = "value"
    ENUM = "enum"
    VIEW = "view"
    HANDLE = "handle"
    CHAR_ARRAY = "char_array"
    ARRAY = "array"
    ADDRESS = "address"
    SKIPPED = "skipped"


@dataclass
class SchemaField:
    """One manifest field resolved against the dump."""

    engine_name: str  # as the dump spells it, such as m_iHealth
    method: str  # the generated C++ getter, such as Health
    offset: int
    size: int
    kind: FieldKind
    cpp_type: str = ""  # value, enum and handle type, or an array's element type
    view_class: str = ""  # the schema class a view returns
    embedded: bool = False  # a view held by value rather than behind a pointer
    length: int = 0  # a fixed array's element count
    engine_type: str = ""  # the dump's type spelling, for the offset comment
    networked: bool = False
    skip_reason: str = ""

    @property
    def is_generated(self) -> bool:
        return self.kind is not FieldKind.SKIPPED


@dataclass
class SchemaClass:
    name: str
    size: int
    owner_link_offset: int
    base: str | None
    is_entity: bool = False
    in_entity: bool = False
    fields: list[SchemaField] = field(default_factory=list)

    @property
    def writable(self) -> bool:
        """Whether a write has a route to the engine's dirty tracking."""
        return self.is_entity or self.owner_link_offset >= 0 or self.in_entity

    @property
    def generated_fields(self) -> list[SchemaField]:
        return [schema_field for schema_field in self.fields if schema_field.is_generated]


def method_name(engine_name: str) -> str:
    """`m_flVelocityModifier` -> `VelocityModifier`."""
    body = engine_name.removeprefix("m_")
    if not body:
        raise ValueError(f"{engine_name}: no name left after m_")

    for length in (3, 2, 1):
        prefix, rest = body[:length], body[length:]
        if prefix in HUNGARIAN_PREFIXES and rest[:1].isupper() and len(rest) >= 3:
            return rest
    return body[0].upper() + body[1:]


def parse_manifest_entry(entry: str) -> tuple[str, str | None, str | None]:
    """`m_state>Offset:Vector` -> (engine name, method override, C++ type override)."""
    name, _, cpp_type = entry.partition(":")
    name, _, method = name.partition(">")
    return name, method or None, cpp_type or None


def cpp_identifier(name: str) -> str:
    """A schema name usable in C++; `CFuncMover::Move_t` is nested in the schema."""
    return name.replace("::", "_")


def enum_underlying_type(size: int) -> str:
    return {1: "int8_t", 2: "int16_t", 4: "int32_t", 8: "int64_t"}.get(size, "int32_t")


def offset_constant(schema_class: SchemaClass, schema_field: SchemaField) -> str:
    return f"k{schema_class.name}_{schema_field.method}"
