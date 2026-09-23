"""How one schema field is spelled in C++: its signatures, its read, its write, its forwarders."""

from dataclasses import dataclass

from voltmod.framework.schemagen.model import (
    FieldKind,
    SchemaClass,
    SchemaField,
    cpp_identifier,
    offset_constant,
)

# A view hands out another object and an address is raw memory: neither is assigned through.
READ_ONLY_KINDS = {FieldKind.VIEW, FieldKind.ADDRESS}


@dataclass(frozen=True, slots=True)
class SetterCode:
    params: str
    wrapper_params: str
    arguments: str  # what a wrapper forwards
    guard: str
    statement: str
    notify: str  # empty for a field the engine does not network


@dataclass(frozen=True, slots=True)
class AccessorCode:
    """The C++ pieces of one generated field; the templates lay them out."""

    name: str
    return_type: str  # spelled inside namespace VoltMod::Schema
    wrapper_return_type: str  # spelled from a wrapper outside it
    getter_params: str
    index_argument: str
    read_guard: str  # empty when the read cannot fail
    read_expression: str
    setter: SetterCode | None


def accessor_code(schema_class: SchemaClass, schema_field: SchemaField) -> AccessorCode | None:
    """The spelling of `schema_field`, or None when it is skipped."""
    if schema_field.kind is FieldKind.SKIPPED:
        return None

    cpp_type, wrapper_type = _types(schema_field)
    constant = offset_constant(schema_class, schema_field)
    takes_index = schema_field.kind is FieldKind.ARRAY
    read_guard, read_expression = _read(schema_field, cpp_type, constant)

    setter = None
    if schema_field.kind not in READ_ONLY_KINDS and schema_class.writable:
        leading_index = "size_t index, " if takes_index else ""
        guard, statement, written_offset = _write(schema_field, cpp_type, constant)
        setter = SetterCode(
            params=f"{leading_index}{cpp_type} value",
            wrapper_params=f"{leading_index}{wrapper_type} value",
            arguments="index, value" if takes_index else "value",
            guard=guard,
            statement=statement,
            notify=_notify_statement(schema_class, schema_field, written_offset),
        )

    return AccessorCode(
        name=schema_field.accessor,
        return_type=cpp_type,
        wrapper_return_type=wrapper_type,
        getter_params="size_t index" if takes_index else "",
        index_argument="index" if takes_index else "",
        read_guard=read_guard,
        read_expression=read_expression,
        setter=setter,
    )


def _types(schema_field: SchemaField) -> tuple[str, str]:
    """The field's C++ type inside namespace VoltMod::Schema, and as a wrapper outside spells it."""
    match schema_field.kind:
        case FieldKind.ENUM:
            name = cpp_identifier(schema_field.cpp_type)
            return name, f"Schema::{name}"
        case FieldKind.VIEW:
            return schema_field.view_class, f"Schema::{schema_field.view_class}"
        case FieldKind.CHARS:
            return "std::string_view", "std::string_view"
        case FieldKind.ADDRESS:
            return "void*", "void*"
        case _:
            return schema_field.cpp_type, schema_field.cpp_type


def _read(schema_field: SchemaField, cpp_type: str, constant: str) -> tuple[str, str]:
    """The guard that returns a default, and the expression that reads the field."""
    match schema_field.kind:
        case FieldKind.ADDRESS:
            return "", f"_base ? MemberPtr<void>(_base, {constant}) : nullptr"
        case FieldKind.VIEW if schema_field.embedded:
            owner = f"_owner, _ownerOffset + {constant}"
            return "!_base", f"{cpp_type}{{MemberPtr<void>(_base, {constant}), {owner}}}"
        case FieldKind.VIEW:
            return "!_base", f"{cpp_type}{{*MemberPtr<void*>(_base, {constant})}}"
        case FieldKind.CHARS:
            # CharBuf is what a fixed engine char[N] means; the setter writes through it too.
            buffer = f"CharBuf<{schema_field.extent}>"
            return "!_base", f"MemberPtr<{buffer}>(_base, {constant})->View()"
        case FieldKind.ARRAY:
            guard = f"!_base || index >= {schema_field.extent}"
            return guard, f"MemberPtr<{cpp_type}>(_base, {constant})[index]"
        case _:
            return "!_base", f"*MemberPtr<{cpp_type}>(_base, {constant})"


def _write(schema_field: SchemaField, cpp_type: str, constant: str) -> tuple[str, str, str]:
    """The guard, the assignment, and the offset handed to dirty tracking."""
    if schema_field.kind is FieldKind.ARRAY:
        return (
            f"!_base || index >= {schema_field.extent}",
            f"MemberPtr<{cpp_type}>(_base, {constant})[index] = value;",
            f"{constant} + static_cast<int32_t>(index * sizeof({cpp_type}))",
        )
    stored = f"CharBuf<{schema_field.extent}>" if schema_field.kind is FieldKind.CHARS else cpp_type
    return "!_base", f"*MemberPtr<{stored}>(_base, {constant}) = value;", constant


def _notify_statement(schema_class: SchemaClass, schema_field: SchemaField, offset: str) -> str:
    # The engine rejects a change reported for a field it does not network.
    if not schema_field.networked:
        return ""
    if schema_class.owner_link_offset >= 0:
        return f"NotifyComponentOwner(_base, {schema_class.name}_kOwnerLinkOffset, {offset});"
    # An entity view owns itself at offset 0, so this one call also covers structs embedded in it.
    return f"NotifyEntity(_owner, _ownerOffset + {offset});"
