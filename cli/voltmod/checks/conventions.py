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


def check_conventions(
    files: Iterable[SourceFile], declaration_headers: frozenset[str] | None = None
) -> list[CheckResult]:
    """Stray forward declarations, anonymous namespaces and using-directives.

    `declaration_headers` may forward-declare; None accepts any `*Types.hpp`.
    """
    forwards, anonymous, directives = [], [], []

    for file in files:
        is_header = file.path.endswith(".hpp")
        if declaration_headers is None:
            declares_types = bool(PLUGIN_DECLARATION_HEADER.search(file.path))
        else:
            declares_types = file.path in declaration_headers

        for number, line in enumerate(file.text.splitlines(), 1):
            where = f"{file.path}:{number}"
            declared = FORWARD_DECLARATION.match(line)
            if is_header and not declares_types and declared:
                definition = DEFINITION.format(re.escape(declared.group(1)))
                if not re.search(definition, file.text, re.MULTILINE):
                    forwards.append(
                        CheckResult.fail(
                            f"{where}: forward declaration `{line.strip()}`",
                            hint="Include the defining header, or use the documented "
                            "*Types.hpp file.",
                        )
                    )
            if ANONYMOUS_NAMESPACE.match(line):
                anonymous.append(
                    CheckResult.fail(
                        f"{where}: anonymous namespace", hint="Use a static file-scope declaration."
                    )
                )
            if USING_DIRECTIVE.match(line):
                directives.append(
                    CheckResult.fail(
                        f"{where}: using-directive `{line.strip()}`",
                        hint="Qualify the name, or use a targeted using-declaration in the .cpp.",
                    )
                )
    return forwards + anonymous + directives


def check_plugins(root: Path) -> list[CheckResult]:
    """Source conventions in a consumer's plugins directory, or in `root` itself."""
    plugins = root / "plugins" if (root / "plugins").is_dir() else root
    if not plugins.is_dir():
        raise VoltmodError(f"no plugins directory under {root.resolve()}")
    base = plugins.relative_to(root).as_posix() if plugins != root else ""
    return check_conventions(read_sources(root, (base,)))
