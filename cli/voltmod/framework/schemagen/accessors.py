"""How one schema field is spelled in C++: its signatures, its read, its write, its forwarders."""

from dataclasses import dataclass, replace

from voltmod.framework.schemagen.model import (
    FieldKind,
    SchemaClass,
    SchemaField,
    cpp_identifier,
    offset_constant,
)


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
    has_setter: bool = False
    setter_params: str = ""
    wrapper_setter_params: str = ""
    setter_arguments: str = ""
    write_guard: str = ""
    write_statement: str = ""
    notify_statement: str = ""


def accessor_code(schema_class: SchemaClass, schema_field: SchemaField) -> AccessorCode | None:
    """The spelling of `schema_field`, or None when it is skipped."""
    match schema_field.kind:
        case FieldKind.SKIPPED:
            return None
        case FieldKind.ENUM:
            name = cpp_identifier(schema_field.cpp_type)
            cpp_type, wrapper_type, assignable = name, f"Schema::{name}", True
        case FieldKind.VIEW:
            name = schema_field.view_class
            cpp_type, wrapper_type, assignable = name, f"Schema::{name}", False
        case FieldKind.CHARS:
            cpp_type = wrapper_type = "std::string_view"
            assignable = True
        case FieldKind.ADDRESS:
            cpp_type = wrapper_type = "void*"
            assignable = False
        case _:
            cpp_type = wrapper_type = schema_field.cpp_type
            assignable = True

    constant = offset_constant(schema_class, schema_field)
    takes_index = schema_field.kind is FieldKind.ARRAY
    read_guard, read_expression = _read(schema_field, cpp_type, constant)
    code = AccessorCode(
        name=schema_field.accessor,
        return_type=cpp_type,
        wrapper_return_type=wrapper_type,
        getter_params="size_t index" if takes_index else "",
        index_argument="index" if takes_index else "",
        read_guard=read_guard,
        read_expression=read_expression,
    )
    if not (assignable and schema_class.writable):
        return code

    leading_index = "size_t index, " if takes_index else ""
    write_guard, write_statement, written_offset = _write(schema_field, cpp_type, constant)
    return replace(
        code,
        has_setter=True,
        setter_params=f"{leading_index}{cpp_type} value",
        wrapper_setter_params=f"{leading_index}{wrapper_type} value",
        setter_arguments="index, value" if takes_index else "value",
        write_guard=write_guard,
        write_statement=write_statement,
        notify_statement=_notify_statement(schema_class, schema_field, written_offset),
    )


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
