"""Mapping one dumped schema field onto a generated field, or marking it skipped."""

from typing import Any

from voltmod.schemagen.model import FieldKind, SchemaField, accessor_name, parse_manifest_entry

BUILTIN_TYPES = {
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

# Types the engine treats as plain values. Anything else is skipped rather than guessed at.
ATOMIC_TYPES = {
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

# Keyed by the C++ spelling, so a manifest `:CppType` override gets its include too.
CPP_INCLUDES = {
    "Vector": "<VoltMod/Engine/EngineTypes.hpp>",
    "QAngle": "<VoltMod/Engine/EngineTypes.hpp>",
}


def describe_field(entry: str, dumped: dict[str, Any], dump: dict[str, Any]) -> SchemaField:
    """Turn one manifest entry and its dumped field into a generated field, or a skipped one."""
    schema_name, accessor, type_override = parse_manifest_entry(entry)
    type_info = dumped["type"]
    type_name = type_info["name"]

    def make(kind: FieldKind, **extra: Any) -> SchemaField:
        return SchemaField(
            schema_name=schema_name,
            accessor=accessor or accessor_name(schema_name),
            offset=dumped["offset"],
            size=dumped["size"],
            kind=kind,
            schema_type=type_name,
            **extra,
        )

    if type_override:
        return make(FieldKind.VALUE, cpp_type=type_override)

    match type_info["category"]:
        case "builtin" if type_name in BUILTIN_TYPES:
            return make(FieldKind.VALUE, cpp_type=BUILTIN_TYPES[type_name])
        case "declared_enum":
            return make(FieldKind.ENUM, cpp_type=type_name)
        case "declared_class" if type_name in dump["classes"]:
            return make(FieldKind.VIEW, view_class=type_name, embedded=True)
        case "pointer" if type_info.get("inner", "") in dump["classes"]:
            return make(FieldKind.VIEW, view_class=type_info["inner"])
        case "fixed_array":
            inner = type_info.get("inner", "")
            extent = int(type_info.get("extent", 0))
            if inner == "char":
                return make(FieldKind.CHARS, extent=extent)
            if inner in BUILTIN_TYPES and extent > 0:
                return make(FieldKind.ARRAY, cpp_type=BUILTIN_TYPES[inner], extent=extent)
        case "atomic":
            atomic = type_info.get("atomic")
            # A CUtlVector hands back its address, so no SDK container reaches a generated header.
            if atomic == "collection_of_t":
                return make(FieldKind.ADDRESS)
            if atomic == "t" and type_name.startswith("CHandle"):
                return make(FieldKind.HANDLE, cpp_type="uint32_t")
            if type_name in ATOMIC_TYPES:
                return make(FieldKind.VALUE, cpp_type=ATOMIC_TYPES[type_name])
        case "builtin" | "declared_class" | "pointer" | "bitfield":
            pass
        case category:
            return make(FieldKind.SKIPPED, skip_reason=f"{category} {type_name}")
    return make(FieldKind.SKIPPED, skip_reason=type_name)
