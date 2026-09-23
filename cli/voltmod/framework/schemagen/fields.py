from typing import Any

from voltmod.framework.schemagen.dump_types import Dump, DumpedField
from voltmod.framework.schemagen.model import (
    FieldKind,
    SchemaField,
    method_name,
    parse_manifest_entry,
)

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
    "Color": "Color",
    "CPlayerSlot": "int32_t",
    "CEntityIndex": "int32_t",
    "CUtlSymbolLarge": "const char*",
    "CUtlString": "const char*",
    "GameTime_t": "float",
    "GameTick_t": "int32_t",
}

# Keyed by the C++ spelling, so a manifest `:CppType` override gets its include too.
TYPE_INCLUDES = {
    "Vector": "<VoltMod/Engine/EngineTypes.hpp>",
    "QAngle": "<VoltMod/Engine/EngineTypes.hpp>",
    "Color": "<VoltMod/Engine/Color.hpp>",
    "EntityRef": "<VoltMod/Engine/EntityRef.hpp>",
    "VoltMod::Team": "<VoltMod/Engine/Team.hpp>",
}


def resolve_field(entry: str, dumped: DumpedField, dump: Dump) -> SchemaField:
    """Turn one manifest entry and its dumped field into a generated field, or a skipped one."""
    engine_name, method, type_override = parse_manifest_entry(entry)
    type_info = dumped["type"]
    type_name = type_info["name"]

    def make(kind: FieldKind, **extra: Any) -> SchemaField:
        extra.setdefault("method", method or method_name(engine_name))
        return SchemaField(
            engine_name=engine_name,
            offset=dumped["offset"],
            size=dumped["size"],
            kind=kind,
            engine_type=type_name,
            networked=dumped["networked"],
            **extra,
        )

    if type_override:
        return make(FieldKind.VALUE, cpp_type=type_override)

    inner = type_info.get("inner", "")
    match type_info["category"]:
        case "SCHEMA_TYPE_BUILTIN":
            if type_name in BUILTIN_TYPES:
                return make(FieldKind.VALUE, cpp_type=BUILTIN_TYPES[type_name])
        case "SCHEMA_TYPE_DECLARED_ENUM":
            return make(FieldKind.ENUM, cpp_type=type_name)
        case "SCHEMA_TYPE_DECLARED_CLASS":
            if type_name in dump["classes"]:
                return make(FieldKind.VIEW, view_class=type_name, embedded=True)
        case "SCHEMA_TYPE_POINTER":
            if inner in dump["classes"]:
                return make(FieldKind.VIEW, view_class=inner)
        case "SCHEMA_TYPE_FIXED_ARRAY":
            length = int(type_info.get("extent", 0))
            if inner == "char":
                return make(FieldKind.CHAR_ARRAY, length=length)
            if inner in BUILTIN_TYPES and length > 0:
                return make(FieldKind.ARRAY, cpp_type=BUILTIN_TYPES[inner], length=length)
        case "SCHEMA_TYPE_ATOMIC":
            atomic = type_info.get("atomic")
            # A CUtlVector hands back its address, so no SDK container reaches a generated header.
            if atomic == "SCHEMA_ATOMIC_COLLECTION_OF_T":
                return make(FieldKind.ADDRESS)
            # A handle reads as an EntityRef; the Ref suffix keeps it apart from the entity itself.
            if atomic == "SCHEMA_ATOMIC_T" and type_name.startswith("CHandle"):
                handle_method = method or method_name(engine_name) + "Ref"
                return make(FieldKind.HANDLE, cpp_type="EntityRef", method=handle_method)
            if type_name in ATOMIC_TYPES:
                return make(FieldKind.VALUE, cpp_type=ATOMIC_TYPES[type_name])
        case "SCHEMA_TYPE_BITFIELD":
            pass
        case category:
            return make(FieldKind.SKIPPED, skip_reason=f"{category} {type_name}")
    # A type the generator does not know is skipped rather than guessed at.
    return make(FieldKind.SKIPPED, skip_reason=type_name)
