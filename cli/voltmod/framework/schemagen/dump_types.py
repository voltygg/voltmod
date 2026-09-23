from typing import Literal, NotRequired, TypedDict


class DumpedType(TypedDict):
    name: str
    category: str  # SCHEMA_TYPE_*
    inner: NotRequired[str]  # a pointer's or array's element type
    extent: NotRequired[int]
    atomic: NotRequired[str]  # SCHEMA_ATOMIC_*


class DumpedField(TypedDict):
    name: str
    offset: int
    size: int
    networked: bool
    type: DumpedType


class DumpedClass(TypedDict):
    size: int
    base: str  # empty for a root class
    chain_offset: int  # the owner link's offset, or -1
    fields: list[DumpedField]


class DumpedEnumItem(TypedDict):
    name: str
    value: int


class DumpedEnum(TypedDict):
    size: int
    items: list[DumpedEnumItem]


class Dump(TypedDict):
    """The schema a host writes to server.json, and the baseline trimmed from it."""

    build: NotRequired[str]
    classes: dict[str, DumpedClass]
    enums: dict[str, DumpedEnum]


class Manifest(TypedDict):
    """schema/manifest.json: the fields to generate, "*" for all, and the wrappers to forward."""

    classes: dict[str, list[str] | Literal["*"]]
    wrappers: NotRequired[dict[str, list[str]]]
