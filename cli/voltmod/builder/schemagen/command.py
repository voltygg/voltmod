"""The `voltmod schemagen` command: read a dump and a manifest, rewrite the generated tree."""

import json
from pathlib import Path
from typing import Annotated, Any

import typer

from voltmod.tools import chunk_by_length, die, run_tool

from . import emit
from .model import Klass, sorted_classes
from .resolve import build_classes, collect_enums, trimmed_dump

app = typer.Typer(help="Generate the schema accessor layer.")

# Leave room below Windows' 32767-character command-line limit.
MAX_COMMAND_LINE = 24000

HEADER_DIR = Path("include/VoltMod/Schema")
# Generated headers sit in their own directory so what is hand-written is obvious.
GENERATED_DIR = HEADER_DIR / "Generated"
GENERATED_SRC_DIR = Path("src/Schema/Generated")
SCHEMA_DIR = Path("schema")

DUMP_FORMAT = 1


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def _read_json(path: Path, what: str) -> dict[str, Any]:
    if not path.is_file():
        die(f"no {what} at {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def _clear(directory: Path, pattern: str) -> None:
    """Generated trees are rewritten wholesale, so a dropped class leaves no orphan behind."""
    for stale in sorted(directory.glob(pattern)):
        if stale.is_file():
            stale.unlink()


def generate(repo: Path, dump: dict[str, Any], manifest: dict[str, Any]) -> str:
    """Rewrite the generated tree under @p repo and return the one-line summary."""
    classes = build_classes(dump, manifest)
    enums = collect_enums(dump, classes)

    headers = repo / HEADER_DIR
    generated = repo / GENERATED_DIR
    sources = repo / GENERATED_SRC_DIR
    _clear(generated, "**/*")
    _clear(sources, "*.cpp")

    for klass in sorted_classes(classes):
        write(generated / f"{klass.name}.hpp", emit.emit_header(klass))
        write(sources / f"{klass.name}.cpp", emit.emit_class_source(klass))
    write(generated / "Enums.hpp", emit.emit_enums(enums))
    write(headers / "Api.hpp", emit.emit_api(classes))
    write(sources / "Layout.cpp", emit.emit_layout_source(classes))
    for wrapper, names in manifest.get("wrappers", {}).items():
        write(generated / "Wrappers" / f"{wrapper}.inc", emit.emit_wrapper(wrapper, names, classes))
    write(
        repo / SCHEMA_DIR / "server.json",
        json.dumps(trimmed_dump(dump, classes, enums), indent=2) + "\n",
    )

    written = sorted(headers.rglob("*.hpp")) + sorted(generated.rglob("*.inc"))
    written += sorted(sources.glob("*.cpp"))
    for batch in chunk_by_length([str(f) for f in written], MAX_COMMAND_LINE):
        run_tool("clang-format", "-i", *batch)

    return _summary(classes, enums)


def _summary(classes: dict[str, Klass], enums: dict[str, Any]) -> str:
    fields = sum(len(k.fields) for k in classes.values())
    skipped = sum(len(k.members) - len(k.fields) for k in classes.values())
    line = f"schemagen: {len(classes)} classes, {len(enums)} enums, {fields} fields"
    return line + (f", {skipped} skipped" if skipped else "")


@app.callback(invoke_without_command=True)
def schemagen(
    dump_path: Annotated[
        str, typer.Option("--dump", help="Schema dump written by the schema_dump command.")
    ],
    root: Annotated[str, typer.Option("--root", help="Repository root to write into.")] = ".",
) -> None:
    """Regenerate the schema accessor layer from a dump."""
    repo = Path(root).resolve()
    manifest = _read_json(repo / SCHEMA_DIR / "manifest.json", "manifest")
    dump = _read_json(Path(dump_path), "dump")
    if dump.get("format") != DUMP_FORMAT:
        die(f"dump format {dump.get('format')} is not supported; expected {DUMP_FORMAT}")

    print(generate(repo, dump, manifest))
