"""What one member looks like in C++.

Every kind of member is described once, as an `Accessor` plus the body of its read and its
write. The declarations in a class body, the definitions in its .cpp and the forwarders in a
curated wrapper are then all spelled from that one description, so adding a kind is one entry
here rather than four parallel branches.
"""

from dataclasses import dataclass

from .model import Klass, Member, cpp_identifier, offset_constant


@dataclass(frozen=True)
class CppType:
    """A type spelled from inside `namespace VoltMod::Schema` and from a wrapper outside it."""

    inner: str
    outer: str


def _plain(name: str) -> CppType:
    return CppType(name, name)


def _scoped(name: str) -> CppType:
    return CppType(name, f"Schema::{name}")


@dataclass(frozen=True)
class Accessor:
    """The signature shape of one member: what it returns, and whether it takes a write."""

    ret: CppType
    value: CppType | None = None  # the setter's value type; None when never assigned wholesale
    index: bool = False  # takes a leading `size_t index`


@dataclass(frozen=True)
class Read:
    guard: str | None  # the condition that returns a default, or None when the read cannot fail
    expr: str
    prelude: tuple[str, ...] = ()


@dataclass(frozen=True)
class Write:
    guard: str
    statement: str
    offset: str  # the offset expression handed to the dirty-tracking call


def accessor_of(member: Member) -> Accessor | None:
    """The signature shape of @p member, or None when it generates nothing."""
    kind = member.kind
    if kind == "skipped":
        return None
    if kind in ("value", "handle"):
        return Accessor(ret=_plain(member.cpp), value=_plain(member.cpp))
    if kind == "enum":
        spelled = _scoped(cpp_identifier(member.cpp))
        return Accessor(ret=spelled, value=spelled)
    if kind == "view":
        return Accessor(ret=_scoped(member.view))
    if kind == "chars":
        return Accessor(ret=_plain("std::string_view"), value=_plain("std::string_view"))
    if kind == "array":
        return Accessor(ret=_plain(member.cpp), value=_plain(member.cpp), index=True)
    if kind == "raw":
        return Accessor(ret=_plain("void*"))
    raise AssertionError(member.kind)


def _read(member: Member, acc: Accessor, constant: str) -> Read:
    kind = member.kind
    if kind == "raw":
        return Read(guard=None, expr=f"_base ? MemberPtr<void>(_base, {constant}) : nullptr")
    if kind == "view":
        if member.embedded:
            made = (f"{member.view}{{MemberPtr<void>(_base, {constant}), _owner, "
                    f"_ownerOffset + {constant}}}")
        else:
            made = f"{member.view}{{*MemberPtr<void*>(_base, {constant})}}"
        return Read(guard="!_base", expr=made)
    if kind == "chars":
        return Read(
            guard="!_base",
            prelude=(f"const char* text = MemberPtr<const char>(_base, {constant});",),
            expr=f"std::string_view(text, ::strnlen(text, {member.extent} - 1))",
        )
    if kind == "array":
        return Read(
            guard=f"!_base || index >= {member.extent}",
            expr=f"MemberPtr<{acc.ret.inner}>(_base, {constant})[index]",
        )
    return Read(guard="!_base", expr=f"*MemberPtr<{acc.ret.inner}>(_base, {constant})")


def _write(member: Member, acc: Accessor, constant: str) -> Write | None:
    if acc.value is None:
        return None
    cpp = acc.ret.inner
    if member.kind == "array":
        return Write(
            guard=f"!_base || index >= {member.extent}",
            statement=f"MemberPtr<{cpp}>(_base, {constant})[index] = value;",
            offset=f"{constant} + static_cast<int32_t>(index * sizeof({cpp}))",
        )
    held = f"CharBuf<{member.extent}>" if member.kind == "chars" else cpp
    return Write(
        guard="!_base",
        statement=f"*MemberPtr<{held}>(_base, {constant}) = value;",
        offset=constant,
    )


def _spell(kind: CppType, outer: bool) -> str:
    return kind.outer if outer else kind.inner


def getter_params(acc: Accessor) -> str:
    return "size_t index" if acc.index else ""


def setter_params(acc: Accessor, outer: bool = False) -> str:
    assert acc.value is not None
    lead = "size_t index, " if acc.index else ""
    return f"{lead}{_spell(acc.value, outer)} value"


def getter_decl(member: Member, acc: Accessor, outer: bool = False) -> str:
    return f"{_spell(acc.ret, outer)} {member.accessor}({getter_params(acc)}) const"


def setter_decl(member: Member, acc: Accessor, outer: bool = False) -> str:
    return f"void Set{member.accessor}({setter_params(acc, outer)}) const"


def declarations(klass: Klass, member: Member) -> list[str]:
    """The lines one member contributes to a generated class body."""
    acc = accessor_of(member)
    if acc is None:
        return [f"// skipped: {member.schema_name} ({member.skip_reason})"]

    out = [f"{getter_decl(member, acc)};"]
    if acc.value is not None and klass.writable:
        out.append(f"{setter_decl(member, acc)};")
    return out


def notify_call(klass: Klass, offset: str) -> list[str]:
    """How a write on this class reaches the engine's dirty tracking."""
    if klass.chain_offset >= 0:
        return [f"    NotifyThroughChain(_base, {klass.name}_kChainOffset, {offset});"]
    # An entity view owns itself at offset 0, so this covers both the entity and the
    # embedded-in-an-entity case with one branch.
    return ["    if (_owner)", f"        NotifyEntity(_owner, _ownerOffset + {offset});"]


def definitions(klass: Klass, member: Member) -> list[str]:
    """The out-of-line definitions one member contributes to its class's .cpp."""
    acc = accessor_of(member)
    if acc is None:
        return []

    constant = offset_constant(klass, member)
    read = _read(member, acc, constant)

    out = [f"{_spell(acc.ret, False)} {klass.name}::{member.accessor}"
           f"({getter_params(acc)}) const", "{"]
    if read.guard:
        out += [f"    if ({read.guard})", "        return {};", ""]
    out += [f"    {line}" for line in read.prelude]
    out += [f"    return {read.expr};", "}", ""]

    write = _write(member, acc, constant) if klass.writable else None
    if write:
        out += [
            f"void {klass.name}::Set{member.accessor}({setter_params(acc)}) const",
            "{",
            f"    if ({write.guard})",
            "        return;",
            "",
            f"    {write.statement}",
        ]
        out += notify_call(klass, write.offset)
        out += ["}", ""]

    return out


def forwarders(klass: Klass, member: Member) -> list[str]:
    """The inline forwarders one member contributes to a curated wrapper."""
    acc = accessor_of(member)
    if acc is None:
        return []

    view = f"Schema::{klass.name}{{_e}}"
    name = member.accessor
    args = "index" if acc.index else ""
    out = [f"{getter_decl(member, acc, outer=True)} {{ return {view}.{name}({args}); }}"]
    if acc.value is not None and klass.writable:
        passed = "index, value" if acc.index else "value"
        out.append(
            f"{setter_decl(member, acc, outer=True)} {{ {view}.Set{name}({passed}); }}"
        )
    return out
