"""Resolving the manifest against a dump into the closed set of classes to generate."""

from typing import Any

from voltmod.errors import VoltmodError
from voltmod.framework.schemagen.fields import describe_field
from voltmod.framework.schemagen.model import (
    ENTITY_ROOT,
    FieldKind,
    SchemaClass,
    parse_manifest_entry,
)


def base_chain(dump: dict[str, Any], name: str) -> list[str]:
    """The single-inheritance chain above `name`, nearest first."""
    chain: list[str] = []
    current = name
    while True:
        dumped = dump["classes"].get(current)
        if not dumped or not dumped["base"]:
            return chain
        current = dumped["base"]
        if current in chain:
            return chain
        chain.append(current)


def resolve_classes(dump: dict[str, Any], manifest: dict[str, Any]) -> dict[str, SchemaClass]:
    """Every manifest class, plus the bases and returned view types they pull in."""
    wanted = dict(manifest["classes"])
    # base_chain walks to the root, so one pass closes the set.
    for name in list(wanted):
        for base in base_chain(dump, name):
            wanted.setdefault(base, [])

    classes = {name: _resolve_class(dump, name, entries) for name, entries in wanted.items()}
    _add_view_classes(dump, classes)
    _mark_embedded_in_entity(classes)
    return classes


def collect_enums(dump: dict[str, Any], classes: dict[str, SchemaClass]) -> dict[str, Any]:
    """Every enum a generated field returns, sorted by name."""
    names = {
        schema_field.cpp_type
        for schema_class in classes.values()
        for schema_field in schema_class.fields
        if schema_field.kind is FieldKind.ENUM
    }
    enums = {}
    for name in sorted(names):
        if name not in dump["enums"]:
            raise VoltmodError(f"a generated field returns enum '{name}', which the dump lacks")
        enums[name] = dump["enums"][name]
    return enums


def baseline_dump(
    dump: dict[str, Any], classes: dict[str, SchemaClass], enums: dict[str, Any]
) -> dict[str, Any]:
    """The dump reduced to what the generator read, committed beside its output."""
    return {
        "build": dump.get("build", ""),
        "classes": {name: dump["classes"][name] for name in sorted(classes)},
        "enums": dict(sorted(enums.items())),
    }


def _empty_class(dump: dict[str, Any], name: str) -> SchemaClass:
    dumped = dump["classes"][name]
    chain = base_chain(dump, name)
    return SchemaClass(
        name=name,
        size=dumped["size"],
        owner_link_offset=dumped["chain_offset"],
        base=chain[0] if chain else None,
        entity_rooted=name == ENTITY_ROOT or ENTITY_ROOT in chain,
    )


def _resolve_class(dump: dict[str, Any], name: str, entries: Any) -> SchemaClass:
    """One manifest class with its selected fields described and its accessors unique."""
    dumped = dump["classes"].get(name)
    if dumped is None:
        raise VoltmodError(f"manifest names class '{name}', which the dump does not have")

    by_name = {entry["name"]: entry for entry in dumped["fields"]}
    schema_class = _empty_class(dump, name)
    selected = list(by_name) if entries == "*" else list(entries)
    for entry in selected:
        schema_name, _, _ = parse_manifest_entry(entry)
        if schema_name not in by_name:
            raise VoltmodError(f"manifest names {name}::{schema_name}, which the dump lacks")
        schema_class.fields.append(describe_field(entry, by_name[schema_name], dump))

    seen: dict[str, str] = {}
    for schema_field in schema_class.fields:
        if schema_field.accessor in seen:
            accessor = schema_field.accessor
            raise VoltmodError(
                f"{name}: '{schema_field.schema_name}' and '{seen[accessor]}' both map to "
                f"{accessor}(); rename one with '>' in the manifest"
            )
        seen[schema_field.accessor] = schema_field.schema_name
    return schema_class


def _add_view_classes(dump: dict[str, Any], classes: dict[str, SchemaClass]) -> None:
    """Add the classes that generated views return, with their bases."""
    while True:
        missing = {
            schema_field.view_class
            for schema_class in classes.values()
            for schema_field in schema_class.fields
            if schema_field.kind is FieldKind.VIEW and schema_field.view_class not in classes
        }
        if not missing:
            return
        for name in missing:
            for pulled in [name, *base_chain(dump, name)]:
                if pulled not in classes:
                    classes[pulled] = _empty_class(dump, pulled)


def _mark_embedded_in_entity(classes: dict[str, SchemaClass]) -> None:
    """An embedded struct gets setters only when every class holding it is rooted in an entity."""
    holders: dict[str, list[str]] = {}
    for schema_class in classes.values():
        for schema_field in schema_class.fields:
            if schema_field.kind is FieldKind.VIEW and schema_field.embedded:
                holders.setdefault(schema_field.view_class, []).append(schema_class.name)
    for name, owners in holders.items():
        classes[name].embeds_in_entity = all(classes[owner].entity_rooted for owner in owners)
