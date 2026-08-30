"""Mapping one dumped schema field onto a generated member, or marking it skipped."""

from typing import Any

from .model import Member, accessor_name, parse_entry

BUILTINS = {
    "bool": "bool",
    "char": "char",
    "int8": "int8_t",
    "uint8": "uint8_t",
    "int16": "int16_t",
    "uint16": "uint16_t",
    "int32": "int32_t",
    "uint32": "uint32_t",
    "int64": "int64_t",
    "uint64": "uint64_t",
    "float32": "float",
    "float64": "double",
}

# Atomic types the engine treats as values. Anything not named here is skipped rather than
# guessed at, so an unmapped type is visible in the generated output instead of silently wrong.
ATOMIC_VALUES = {
    "Vector": "Vector",
    "VectorWS": "Vector",
    "QAngle": "QAngle",
    "Color": "uint32_t",
    "CPlayerSlot": "int32_t",
    "CEntityIndex": "int32_t",
    "CUtlSymbolLarge": "const char*",
    "CUtlString": "const char*",
    "GameTime_t": "float",
    "GameTick_t": "int32_t",
}


# The header a mapped C++ type needs. Keyed by the C++ spelling rather than the schema one, so a
# manifest `:CppType` override picks its include up the same way a table entry does.
CPP_INCLUDES = {
    "Vector": "<VoltMod/Engine/EngineTypes.hpp>",
    "QAngle": "<VoltMod/Engine/EngineTypes.hpp>",
}


def describe(entry: str, raw: dict[str, Any], dump: dict[str, Any]) -> Member:
    """Turn one dump field into a generated member, or mark it skipped."""
    schema_name, rename, cpp_override = parse_entry(entry)
    kind = raw["type"]
    common = {
        "schema_name": schema_name,
        "accessor": rename or accessor_name(schema_name),
        "offset": raw["offset"],
        "size": raw["size"],
        "note": kind["name"],
    }

    def skipped(reason: str) -> Member:
        return Member(**common, kind="skipped", skip_reason=reason)

    if cpp_override:
        return Member(**common, kind="value", cpp=cpp_override)

    category = kind["category"]

    if category == "builtin":
        mapped = BUILTINS.get(kind["name"])
        return Member(**common, kind="value", cpp=mapped) if mapped else skipped(kind["name"])

    if category == "declared_enum":
        return Member(**common, kind="enum", cpp=kind["name"])

    if category == "declared_class":
        if kind["name"] not in dump["classes"]:
            return skipped(kind["name"])
        return Member(**common, kind="view", view=kind["name"], embedded=True)

    if category == "pointer":
        inner = kind.get("inner", "")
        if inner not in dump["classes"]:
            return skipped(kind["name"])
        return Member(**common, kind="view", view=inner)

    if category == "fixed_array":
        inner = kind.get("inner", "")
        extent = int(kind.get("extent", 0))
        if inner == "char":
            return Member(**common, kind="chars", extent=extent)
        mapped = BUILTINS.get(inner)
        if not mapped or extent <= 0:
            return skipped(kind["name"])
        return Member(**common, kind="array", cpp=mapped, extent=extent)

    if category == "bitfield":
        # v1 has no bitfield accessor; the name already carries the width.
        return skipped(kind["name"])

    if category == "atomic":
        atomic = kind.get("atomic")
        if atomic == "collection_of_t":
            # A CUtlVector's element handling is the consumer's business; bake the offset and
            # hand back the address so no SDK container type reaches a generated header.
            return Member(**common, kind="raw")
        if atomic == "t" and kind["name"].startswith("CHandle"):
            return Member(**common, kind="handle", cpp="uint32_t")
        mapped = ATOMIC_VALUES.get(kind["name"])
        return Member(**common, kind="value", cpp=mapped) if mapped else skipped(kind["name"])

    return skipped(f"{category} {kind['name']}")
