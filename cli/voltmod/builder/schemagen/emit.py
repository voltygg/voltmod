"""What one generated file looks like."""

from typing import Any

from voltmod.tools import die

from .accessors import declarations, definitions, forwarders
from .fields import CPP_INCLUDES
from .model import (
    BANNER,
    ENTITY_ROOT,
    Klass,
    cpp_identifier,
    enum_underlying,
    offset_constant,
    sorted_classes,
)


def _unit(lines: list[str]) -> str:
    return "\n".join(lines)


def _header_includes(klass: Klass) -> list[str]:
    includes = {"<VoltMod/Schema/View.hpp>", "<cstdint>"}
    if klass.base:
        includes.add(f"<VoltMod/Schema/Generated/{klass.base}.hpp>")
    for member in klass.members:
        if member.kind == "view":
            includes.add(f"<VoltMod/Schema/Generated/{member.view}.hpp>")
        if member.kind == "enum":
            includes.add("<VoltMod/Schema/Generated/Enums.hpp>")
        if member.kind == "chars":
            includes.add("<string_view>")
        if member.kind == "array":
            includes.add("<cstddef>")
        if member.kind == "value" and member.cpp in CPP_INCLUDES:
            includes.add(CPP_INCLUDES[member.cpp])
    return [f"#include {inc}" for inc in sorted(includes)]


def _constructors(klass: Klass) -> list[str]:
    if klass.name != ENTITY_ROOT:
        parent = klass.base or "View"
        return [f"    using {parent}::{parent};"]

    # An entity is its own notification target, so a write anywhere inside it - including
    # inside a struct it embeds - knows which entity to dirty and at what offset.
    return [
        f"    {klass.name}() = default;",
        "",
        f"    explicit {klass.name}(::{ENTITY_ROOT}* entity) noexcept",
        "        : View(entity, entity, 0)",
        "    {",
        "    }",
        "",
        f"    explicit {klass.name}(void* base) noexcept",
        f"        : View(base, static_cast<::{ENTITY_ROOT}*>(base), 0)",
        "    {",
        "    }",
    ]


def emit_header(klass: Klass) -> str:
    """One `include/VoltMod/Schema/Generated/<Class>.hpp`."""
    lines = [BANNER, "#pragma once", ""]
    lines += _header_includes(klass)
    lines += ["", "namespace VoltMod::Schema", "{", ""]

    lines.append(f"/** Frame-local view over an engine {klass.name}; never store one. */")
    lines.append(f"class {klass.name} : public {klass.base or 'View'}")
    lines += ["{", "public:"]
    lines += _constructors(klass)

    body: list[str] = []
    for member in klass.members:
        body.append("")
        body += declarations(klass, member)
    lines += [f"    {line}" if line else "" for line in body]

    lines += ["};", "", "}  // namespace VoltMod::Schema", ""]
    return _unit(lines)


def emit_enums(enums: dict[str, Any]) -> str:
    """`include/VoltMod/Schema/Generated/Enums.hpp`."""
    lines = [BANNER, "#pragma once", "", "#include <cstdint>", "",
             "namespace VoltMod::Schema", "{", ""]
    for name, info in enums.items():
        lines.append(f"/** Schema enum {name}. */")
        lines.append(f"enum class {cpp_identifier(name)} : {enum_underlying(info['size'])}")
        lines.append("{")
        seen: set[int] = set()
        for item in info["items"]:
            # C++ rejects duplicate enumerators; the schema allows aliases.
            if item["value"] in seen:
                lines.append(f"    // alias: {item['name']} = {item['value']}")
                continue
            seen.add(item["value"])
            lines.append(f"    {item['name']} = {item['value']},")
        lines += ["};", ""]
    lines += ["}  // namespace VoltMod::Schema", ""]
    return _unit(lines)


def emit_api(classes: dict[str, Klass]) -> str:
    """`include/VoltMod/Schema/Api.hpp`: the module's public surface."""
    lines = [BANNER, "#pragma once", ""]
    lines += [
        "#include <VoltMod/Schema/Generated/Enums.hpp>",
        "#include <VoltMod/Schema/Layout.hpp>",
        "#include <VoltMod/Schema/Notify.hpp>",
        "#include <VoltMod/Schema/View.hpp>",
    ]
    lines += [f"#include <VoltMod/Schema/Generated/{k.name}.hpp>" for k in sorted_classes(classes)]
    lines.append("")
    return _unit(lines)


def emit_wrapper(wrapper: str, class_names: list[str], classes: dict[str, Klass]) -> str:
    """One `include/VoltMod/Schema/Generated/Wrappers/<Wrapper>.inc`.

    Included inside the wrapper's class body, so it is a fragment rather than a header: the
    forwarders read the wrapper's own `_e`, and every schema field reaches plugins without a
    hand-written line here.
    """
    lines = [
        BANNER.rstrip("\n"),
        f"// Schema accessors forwarded onto VoltMod::{wrapper}. Included inside the class body.",
    ]
    for name in class_names:
        klass = classes.get(name)
        if klass is None:
            die(f"manifest wrapper '{wrapper}' names class '{name}', which is not generated")
        emitted = [line for m in klass.members for line in forwarders(klass, m)]
        if emitted:
            lines += ["", f"// {klass.name}"] + emitted
    lines.append("")
    return _unit(lines)


def _source_includes(klass: Klass) -> list[str]:
    includes = {
        "<VoltMod/Engine/MemoryAccess.hpp>",
        f"<VoltMod/Schema/Generated/{klass.name}.hpp>",
        "<VoltMod/Schema/Layout.hpp>",
        "<VoltMod/Schema/Notify.hpp>",
    }
    for member in klass.members:
        if member.kind == "view":
            includes.add(f"<VoltMod/Schema/Generated/{member.view}.hpp>")
        if member.kind == "chars":
            includes.add("<VoltMod/Core/CharBuf.hpp>")
    return [f"#include {inc}" for inc in sorted(includes)]


def _offset_constants(klass: Klass) -> list[str]:
    lines = [f"// ---- {klass.name}, {klass.size} bytes " + "-" * max(0, 50 - len(klass.name))]
    if klass.chain_offset >= 0:
        lines.append(f"static constexpr int32_t {klass.name}_kChainOffset = {klass.chain_offset};")
    lines += [
        f"static constexpr int32_t {offset_constant(klass, m)} = {m.offset};  // {m.note}"
        for m in klass.fields
    ]
    lines.append("")
    return lines


def _field_layout(klass: Klass) -> list[str]:
    """The class's slice of the verifier's table, defined beside the offsets it restates."""
    fields = klass.fields
    if not fields:
        return []

    # `extern` on the definition because a namespace-scope const is internal by default;
    # Layout.cpp names this array so the offsets stay defined once, beside the accessors
    # that use them, rather than being restated in the table.
    lines = [
        f"extern const FieldLayout {klass.name}_kFields[{len(fields)}];",
        f"const FieldLayout {klass.name}_kFields[{len(fields)}] = {{",
    ]
    lines += [
        f'    {{.Name = "{m.schema_name}", .Offset = {offset_constant(klass, m)}, '
        f".Size = {m.size}}},"
        for m in fields
    ]
    return lines + ["};", ""]


def emit_class_source(klass: Klass) -> str:
    """One `src/Schema/Generated/<Class>.cpp`."""
    lines = [BANNER.rstrip("\n"), ""]
    lines += _source_includes(klass)
    lines += ["", "namespace VoltMod::Schema", "{", ""]
    lines += _offset_constants(klass)
    for member in klass.members:
        lines += definitions(klass, member)
    lines += _field_layout(klass)
    lines += ["}  // namespace VoltMod::Schema", ""]
    return _unit(lines)


def emit_layout_source(classes: dict[str, Klass]) -> str:
    """`src/Schema/Generated/Layout.cpp`: the table the load-time verifier walks."""
    ordered = sorted_classes(classes)
    lines = [
        BANNER.rstrip("\n"),
        "",
        "#include <VoltMod/Schema/Layout.hpp>",
        "",
        "namespace VoltMod::Schema",
        "{",
        "",
        "// Defined beside the accessors that share their offsets, in each class's own file.",
    ]
    lines += [
        f"extern const FieldLayout {k.name}_kFields[{len(k.fields)}];" for k in ordered if k.fields
    ]
    lines.append("")

    lines.append("static const ClassLayout kClasses[] = {")
    for klass in ordered:
        span = f"{{{klass.name}_kFields, {len(klass.fields)}}}" if klass.fields else "{}"
        lines.append(
            f'    {{.Name = "{klass.name}", .Size = {klass.size}, '
            f".ChainOffset = {klass.chain_offset}, .Fields = {span}}},"
        )
    lines += ["};", ""]

    lines += [
        "std::span<const ClassLayout> GeneratedLayout()",
        "{",
        "    return kClasses;",
        "}",
        "",
        "}  // namespace VoltMod::Schema",
        "",
    ]
    return _unit(lines)
