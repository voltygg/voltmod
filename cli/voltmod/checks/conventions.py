"""Source conventions every VoltMod C++ tree follows, the framework's and a consumer's plugins."""

import re
from collections.abc import Iterable, Iterator
from pathlib import Path
from typing import NamedTuple

from voltmod.checks.results import CheckResult
from voltmod.errors import VoltmodError
from voltmod.toolchain.clang_format import CPP_SUFFIXES

# A plugin's layout is its own, so its declaration header is matched by file name.
PLUGIN_DECLARATION_HEADER = re.compile(r"(^|/)\w*Types\.hpp$")

FORWARD_DECLARATION = re.compile(r"^(?:class|struct)\s+(\w+);")
DEFINITION = r"^(?:class|struct)\s+{}\b\s*(?!;)"
ANONYMOUS_NAMESPACE = re.compile(r"^[ \t]*namespace[ \t]*(\{[ \t]*)?$")
USING_DIRECTIVE = re.compile(r"^[ \t]*using\s+namespace\b")

_STATIC_HINT = "Use a static file-scope declaration."
_USING_HINT = "Qualify the name, or use a targeted using-declaration in the .cpp."


class SourceFile(NamedTuple):
    path: str  # relative to the repo root, with forward slashes
    module: str | None
    text: str


def read_sources(root: Path, bases: Iterable[str]) -> Iterator[SourceFile]:
    for base in bases:
        directory = root / base
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.suffix not in CPP_SUFFIXES:
                continue
            parts = path.relative_to(directory).parts
            yield SourceFile(
                path.relative_to(root).as_posix(),
                parts[0] if len(parts) > 1 else None,
                path.read_text(encoding="utf-8", errors="replace"),
            )


def source_lines(
    files: Iterable[SourceFile], prefix: str | tuple[str, ...] = ""
) -> Iterator[tuple[SourceFile, int, str]]:
    """Every numbered line of the files whose path starts with `prefix`."""
    for file in files:
        if file.path.startswith(prefix):
            for number, line in enumerate(file.text.splitlines(), 1):
                yield file, number, line


def check_conventions(
    files: Iterable[SourceFile], declaration_headers: frozenset[str] | None = None
) -> list[CheckResult]:
    """Stray forward declarations, anonymous namespaces and using-directives.

    `declaration_headers` may forward-declare; None accepts any `*Types.hpp`.
    """
    files = list(files)
    return (
        _forward_declarations(files, declaration_headers)
        + _anonymous_namespaces(files)
        + _using_directives(files)
    )


def _forward_declarations(
    files: list[SourceFile], declaration_headers: frozenset[str] | None
) -> list[CheckResult]:
    hint = "Include the defining header, or use the documented *Types.hpp file."
    results = []
    for file, number, line in source_lines(files):
        declared = FORWARD_DECLARATION.match(line)
        if not declared or not file.path.endswith(".hpp"):
            continue
        if _may_forward_declare(file.path, declaration_headers):
            continue
        # Declaring a name the same header goes on to define is only an ordering aid.
        definition = DEFINITION.format(re.escape(declared.group(1)))
        if not re.search(definition, file.text, re.MULTILINE):
            message = f"{file.path}:{number}: forward declaration `{line.strip()}`"
            results.append(CheckResult.fail(message, hint))
    return results


def _may_forward_declare(path: str, declaration_headers: frozenset[str] | None) -> bool:
    if declaration_headers is None:
        return bool(PLUGIN_DECLARATION_HEADER.search(path))
    return path in declaration_headers


def _anonymous_namespaces(files: list[SourceFile]) -> list[CheckResult]:
    return [
        CheckResult.fail(f"{file.path}:{number}: anonymous namespace", _STATIC_HINT)
        for file, number, line in source_lines(files)
        if ANONYMOUS_NAMESPACE.match(line)
    ]


def _using_directives(files: list[SourceFile]) -> list[CheckResult]:
    return [
        CheckResult.fail(f"{file.path}:{number}: using-directive `{line.strip()}`", _USING_HINT)
        for file, number, line in source_lines(files)
        if USING_DIRECTIVE.match(line)
    ]


def check_plugins(root: Path) -> list[CheckResult]:
    """Source conventions in a consumer's plugins directory, or in `root` itself."""
    plugins = root / "plugins" if (root / "plugins").is_dir() else root
    if not plugins.is_dir():
        raise VoltmodError(f"no plugins directory under {root.resolve()}")
    base = plugins.relative_to(root).as_posix() if plugins != root else ""
    return check_conventions(read_sources(root, (base,)))
