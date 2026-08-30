"""Resolving the manifest against a dump into the closed set of classes to generate."""

from typing import Any

from voltmod.tools import die

from .fields import describe
from .model import ENTITY_ROOT, Klass, parse_entry


def bases_of(dump: dict[str, Any], name: str) -> list[str]:
    """The single-inheritance chain above @p name, nearest first."""
    chain: list[str] = []
    current = name
    while True:
        klass = dump["classes"].get(current)
        if not klass or not klass["bases"]:
            return chain
        current = klass["bases"][0]["name"]
        if current in chain:  # defensive: a cycle would hang the walk
            return chain
        chain.append(current)


def empty_klass(dump: dict[str, Any], name: str) -> Klass:
    """The class with its inheritance and replication facts filled in, but no members yet."""
    raw = dump["classes"][name]
    chain = bases_of(dump, name)
    return Klass(
        name=name,
        size=raw["size"],
        chain_offset=raw["chain_offset"],
        base=chain[0] if chain else None,
        entity_rooted=name == ENTITY_ROOT or ENTITY_ROOT in chain,
    )


def _with_bases(dump: dict[str, Any], wanted: dict[str, Any]) -> dict[str, Any]:
    """Bases must exist as generated types so the C++ inheritance matches the schema's.

    One pass is the whole closure: `bases_of` walks to the root, so a base's own ancestors are
    a suffix of the chain that pulled it in and are already here.
    """
    for name in list(wanted):
        for base in bases_of(dump, name):
            wanted.setdefault(base, [])
    return wanted


def _resolve(dump: dict[str, Any], name: str, entries: Any) -> Klass:
    """One manifest class with its selected fields described and its accessors checked unique."""
    raw = dump["classes"].get(name)
    if raw is None:
        die(f"manifest names class '{name}', which the dump does not have")

    by_name = {f["name"]: f for f in raw["fields"]}
    klass = empty_klass(dump, name)

    selected = [f["name"] for f in raw["fields"]] if entries == "*" else list(entries)
    for entry in selected:
        schema_name, _, _ = parse_entry(entry)
        found = by_name.get(schema_name)
        if found is None:
            die(f"manifest names {name}::{schema_name}, which the dump does not have")
        klass.members.append(describe(entry, found, dump))

    seen: dict[str, str] = {}
    for member in klass.members:
        if member.accessor in seen:
            die(
                f"{name}: '{member.schema_name}' and '{seen[member.accessor]}' both map to "
                f"{member.accessor}(); rename one with '>' in the manifest"
            )
        seen[member.accessor] = member.schema_name

    return klass


def _pull_in_views(dump: dict[str, Any], classes: dict[str, Klass]) -> None:
    """Views a member returns must be generated too, so pull them in - and their bases."""
    while True:
        missing = {
            m.view
            for k in classes.values()
            for m in k.members
            if m.kind == "view" and m.view not in classes
        }
        if not missing:
            return
        for name in missing:
            for pulled in [name, *bases_of(dump, name)]:
                if pulled not in classes:
                    classes[pulled] = empty_klass(dump, pulled)


def _mark_embedded(classes: dict[str, Klass]) -> None:
    """A struct embedded by value replicates through whatever holds it, so it may only expose
    setters when every holder is itself entity-rooted."""
    holders: dict[str, list[str]] = {}
    for klass in classes.values():
        for member in klass.members:
            if member.kind == "view" and member.embedded:
                holders.setdefault(member.view, []).append(klass.name)
    for name, owners in holders.items():
        classes[name].embeds_in_entity = all(classes[o].entity_rooted for o in owners)


def build_classes(dump: dict[str, Any], manifest: dict[str, Any]) -> dict[str, Klass]:
    """Resolve every manifest class, plus the bases and inner types they pull in."""
    wanted = _with_bases(dump, dict(manifest["classes"]))
    classes = {name: _resolve(dump, name, entries) for name, entries in wanted.items()}
    _pull_in_views(dump, classes)
    _mark_embedded(classes)
    return classes


def collect_enums(dump: dict[str, Any], classes: dict[str, Klass]) -> dict[str, Any]:
    """Every enum a generated member returns."""
    names = {m.cpp for k in classes.values() for m in k.members if m.kind == "enum"}
    out = {}
    for name in sorted(names):
        found = dump["enums"].get(name)
        if found is None:
            die(f"a generated field returns enum '{name}', which the dump does not have")
        out[name] = found
    return out


def trimmed_dump(
    dump: dict[str, Any], classes: dict[str, Klass], enums: dict[str, Any]
) -> dict[str, Any]:
    """The dump reduced to what the generator read, for committing beside the output."""
    return {
        "format": dump["format"],
        "scopes": dump["scopes"],
        "classes": {name: dump["classes"][name] for name in sorted(classes)},
        "enums": dict(sorted(enums.items())),
    }
