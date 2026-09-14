"""Rendering the schema accessor layer from a dump and manifest, then writing or checking it."""

import json
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from voltmod.cpp_sources import CPP_SUFFIXES, format_cpp_files
from voltmod.errors import VoltmodError
from voltmod.files import write_if_changed, write_or_check
from voltmod.project import load_template
from voltmod.schemagen.accessors import AccessorCode, accessor_code
from voltmod.schemagen.fields import CPP_INCLUDES
from voltmod.schemagen.model import (
    ENTITY_ROOT,
    FieldKind,
    SchemaClass,
    cpp_identifier,
    enum_underlying_type,
    offset_constant,
)
from voltmod.schemagen.resolve import baseline_dump, collect_enums, resolve_classes

HEADER_DIR = Path("include/VoltMod/Schema")
# Generated headers stay apart from the hand-written ones.
GENERATED_HEADER_DIR = HEADER_DIR / "Generated"
GENERATED_SOURCE_DIR = Path("src/Schema/Generated")
MANIFEST = Path("schema/manifest.json")
# The Windows and Linux builds of one game version lay classes out differently.
BASELINES = {platform: Path(f"schema/server.{platform}.json") for platform in ("windows", "linux")}


@dataclass(frozen=True, slots=True)
class SchemaOutput:
    files: dict[Path, str]  # repo-relative path -> text; C++ is formatted when written
    summary: str


def render_outputs(dump: dict[str, Any], manifest: dict[str, Any], platform: str) -> SchemaOutput:
    classes = resolve_classes(dump, manifest)
    enums = collect_enums(dump, classes)
    ordered = [classes[name] for name in sorted(classes)]
    game_build = dump.get("build", "unknown")

    codes = {
        name: [accessor_code(schema_class, field) for field in schema_class.fields]
        for name, schema_class in classes.items()
    }
    files: dict[Path, str] = {}
    for schema_class in ordered:
        shared = {
            "schema_class": schema_class,
            "fields": list(zip(schema_class.fields, codes[schema_class.name])),
            "entity_root": ENTITY_ROOT,
            "offset_constant": offset_constant,
        }
        files[GENERATED_HEADER_DIR / f"{schema_class.name}.hpp"] = _render(
            "class.hpp.j2", includes=_header_includes(schema_class), **shared
        )
        files[GENERATED_SOURCE_DIR / platform / f"{schema_class.name}.cpp"] = _render(
            "class.cpp.j2", includes=_source_includes(schema_class), **shared
        )
    files[GENERATED_HEADER_DIR / "Enums.hpp"] = _render("enums.hpp.j2", enums=_enum_listings(enums))
    files[HEADER_DIR / "Api.hpp"] = _render("api.hpp.j2", classes=ordered)
    files[GENERATED_SOURCE_DIR / platform / "Layout.cpp"] = _render(
        "layout.cpp.j2", classes=ordered, game_build=game_build
    )
    for wrapper, names in manifest.get("wrappers", {}).items():
        wrapped = _wrapped_classes(wrapper, names, codes, classes)
        files[GENERATED_HEADER_DIR / "Wrappers" / f"{wrapper}.inc"] = _render(
            "wrapper.inc.j2", wrapper=wrapper, classes=wrapped
        )
    files[BASELINES[platform]] = json.dumps(baseline_dump(dump, classes, enums), indent=2) + "\n"
    return SchemaOutput(files, _summary(classes, enums, game_build))


def write_outputs(
    repo: Path, files: dict[Path, str], platform: str, *, check: bool = False
) -> None:
    """Format and write @p files, deleting stale ones; with @p check, fail on any difference."""
    expected = {repo / relative for relative in files}
    stale = [path for path in _existing_generated_files(repo, platform) if path not in expected]
    if check and stale:
        raise VoltmodError(f"stale generated files: {', '.join(map(str, stale))}")
    for path in stale:
        path.unlink()

    for relative, text in _format_cpp(repo, files).items():
        write_or_check(repo / relative, text, check=check)


def _format_cpp(repo: Path, files: dict[Path, str]) -> dict[Path, str]:
    """@p files with the C++ ones formatted by one clang-format run."""
    scratch_parent = repo / "build"
    scratch_parent.mkdir(exist_ok=True)
    # Inside the repo, so clang-format finds the .clang-format the real paths would.
    with tempfile.TemporaryDirectory(dir=scratch_parent) as scratch:
        staged = {
            relative: Path(scratch) / relative
            for relative in files
            if relative.suffix in CPP_SUFFIXES
        }
        for relative, path in staged.items():
            write_if_changed(path, files[relative])
        format_cpp_files(list(staged.values()))
        # Bytes, so Windows newline translation cannot change what clang-format wrote.
        formatted = {relative: path.read_bytes().decode() for relative, path in staged.items()}
    return files | formatted


def _render(template: str, **fields: Any) -> str:
    return load_template(f"schemagen/{template}").render(**fields)


def _existing_generated_files(repo: Path, platform: str) -> list[Path]:
    headers = [path for path in (repo / GENERATED_HEADER_DIR).rglob("*") if path.is_file()]
    return headers + sorted((repo / GENERATED_SOURCE_DIR / platform).glob("*.cpp"))


def _header_includes(schema_class: SchemaClass) -> list[str]:
    includes = {"<VoltMod/Schema/View.hpp>", "<cstdint>"}
    if schema_class.base:
        includes.add(f"<VoltMod/Schema/Generated/{schema_class.base}.hpp>")
    for field in schema_class.fields:
        match field.kind:
            case FieldKind.VIEW:
                includes.add(f"<VoltMod/Schema/Generated/{field.view_class}.hpp>")
            case FieldKind.ENUM:
                includes.add("<VoltMod/Schema/Generated/Enums.hpp>")
            case FieldKind.CHARS:
                includes.add("<string_view>")
            case FieldKind.ARRAY:
                includes.add("<cstddef>")
            case FieldKind.VALUE if field.cpp_type in CPP_INCLUDES:
                includes.add(CPP_INCLUDES[field.cpp_type])
    return sorted(includes)


def _source_includes(schema_class: SchemaClass) -> list[str]:
    includes = {
        "<VoltMod/Engine/Memory/MemoryAccess.hpp>",
        f"<VoltMod/Schema/Generated/{schema_class.name}.hpp>",
        "<VoltMod/Schema/Layout.hpp>",
        "<VoltMod/Schema/Notify.hpp>",
    }
    for field in schema_class.fields:
        if field.kind is FieldKind.VIEW:
            includes.add(f"<VoltMod/Schema/Generated/{field.view_class}.hpp>")
        if field.kind is FieldKind.CHARS:
            includes.add("<VoltMod/Core/CharBuf.hpp>")
    return sorted(includes)


def _enum_listings(enums: dict[str, Any]) -> list[dict[str, Any]]:
    listings = []
    for name, info in enums.items():
        seen: set[int] = set()
        items = []
        for item in info["items"]:
            # C++ rejects duplicate enumerators; the schema allows aliases.
            alias = item["value"] in seen
            items.append({"name": item["name"], "value": item["value"], "alias": alias})
            seen.add(item["value"])
        listings.append({
            "schema_name": name,
            "name": cpp_identifier(name),
            "underlying_type": enum_underlying_type(info["size"]),
            "items": items,
        })
    return listings


def _wrapped_classes(
    wrapper: str,
    names: list[str],
    codes: dict[str, list[AccessorCode | None]],
    classes: dict[str, SchemaClass],
) -> list[tuple[SchemaClass, list[AccessorCode]]]:
    wrapped = []
    for name in names:
        if name not in classes:
            raise VoltmodError(f"wrapper '{wrapper}' names '{name}', which is not generated")
        if generated := [code for code in codes[name] if code]:
            wrapped.append((classes[name], generated))
    return wrapped


def _summary(classes: dict[str, SchemaClass], enums: dict[str, Any], game_build: str) -> str:
    generated = sum(len(schema_class.generated_fields) for schema_class in classes.values())
    skipped = sum(len(schema_class.fields) for schema_class in classes.values()) - generated
    line = (
        f"schemagen: game build {game_build}, {len(classes)} classes, "
        f"{len(enums)} enums, {generated} fields"
    )
    return line + (f", {skipped} skipped" if skipped else "")
