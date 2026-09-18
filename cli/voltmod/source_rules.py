"""Module layering and source conventions, for the framework and for consumer plugins."""

import re
from collections.abc import Iterable, Iterator
from pathlib import Path
from typing import NamedTuple

from voltmod.check_results import CheckResult
from voltmod.cpp_sources import CPP_SUFFIXES
from voltmod.errors import VoltmodError

FRAMEWORK_SOURCE_DIRS = ("include/VoltMod", "src")

# Transitive dependencies are listed too: this table is the layering.
ALLOWED_DEPENDENCIES: dict[str, set[str]] = {
    "Core": set(),
    "Engine": {"Core"},
    "Schema": {"Core", "Engine"},
    "Entities": {"Core", "Engine", "Schema"},
    "Events": {"Core", "Engine", "Entities"},
    "Messaging": {"Core", "Engine", "Entities", "Events"},
    "Players": {"Core", "Engine", "Entities"},
    "Hooks": {"Core", "Engine", "Schema", "Entities", "Events", "Players", "Unsafe"},
    "Ui": {"Core", "Engine", "Schema", "Entities", "Hooks", "Unsafe"},
    "Workshop": {"Core", "Engine", "Players", "Unsafe"},
    "Commands": {"Core", "Engine", "Entities", "Players", "Messaging"},
    "Menu": {"Core", "Engine", "Entities", "Players", "Messaging", "Hooks", "Ui", "Workshop"},
    "Http": {"Core"},
    "Database": {"Core"},
    "Unsafe": {"Core", "Engine"},
    # Boundary interfaces only; Engine holds the ISmmAPI and IKHook forward declarations they name.
    "Host": {"Core", "Engine"},
    "App": {
        "Core", "Engine", "Schema", "Entities", "Events", "Messaging", "Players", "Hooks",
        "Ui", "Workshop", "Commands", "Menu", "Http", "Database", "Unsafe", "Host",
    },
}

INCLUDE = re.compile(r'#\s*include\s*[<"]VoltMod/([A-Za-z0-9_]+)/([^>"]+)[>"]')
# A module's Api.hpp gathers its public types; its includes are not dependencies.
API_HEADER = re.compile(r"^include/VoltMod/[A-Za-z0-9_]+/Api\.hpp$")

COMPOSITION_ROOT_INCLUDE = re.compile(r'#\s*include\s*[<"]VoltMod/(Runtime|Api)\.hpp[>"]')
COMPOSITION_ROOT_MODULES = {"App"}

CORE_PATHS = ("include/VoltMod/Core/", "src/Core/")
ENGINE_INCLUDE = re.compile(
    r'#\s*include\s*[<"](ISmmPlugin\.h|tier0/|eiface\.h|entity2/|schemasystem/|icvar\.h|Color\.h)'
)

# Named, not matched: a *Types.hpp pattern would also exempt EventTypes.hpp and ConVarTypes.hpp.
FRAMEWORK_DECLARATION_HEADERS = frozenset({"include/VoltMod/Engine/EngineTypes.hpp"})
# A plugin's layout is its own, so its declaration header is matched by file name.
PLUGIN_DECLARATION_HEADER = re.compile(r"(^|/)\w*Types\.hpp$")

# The host and each plugin are separately compiled, with their own CRT and allocator, so no
# std:: type, template or owned object may appear in a boundary signature.
HOST_BOUNDARY_PATH = "include/VoltMod/Host/"
HOST_BOUNDARY_INCLUDES = frozenset({"<cstddef>", "<cstdint>", "<VoltMod/Engine/EngineTypes.hpp>"})
ANY_INCLUDE = re.compile(r'^\s*#\s*include\s*([<"][^>"]+[>"])')

FORWARD_DECLARATION = re.compile(r"^(?:class|struct)\s+(\w+);")
DEFINITION = r"^(?:class|struct)\s+{}\b\s*(?!;)"
ANONYMOUS_NAMESPACE = re.compile(r"^[ \t]*namespace[ \t]*(\{[ \t]*)?$")
USING_DIRECTIVE = re.compile(r"^[ \t]*using\s+namespace\b")


class SourceFile(NamedTuple):
    path: str  # relative to the repo root, with forward slashes
    module: str | None
    text: str


def layering_table() -> str:
    """ALLOWED_DEPENDENCIES as the block CLAUDE.md and docs/architecture.md quote."""
    lines = []
    for module, allowed in ALLOWED_DEPENDENCIES.items():
        if module == "App":
            depends = "every module"
        else:
            names = [name for name in ALLOWED_DEPENDENCIES if name in allowed]
            depends = ", ".join(names) or "nothing"
        lines.append(f"{module:<10} -> {depends}")
    return "\n".join(lines)


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


def module_dependencies(
    files: Iterable[SourceFile], modules: Iterable[str]
) -> tuple[dict[str, set[str]], dict[tuple[str, str], str]]:
    """Each module's dependencies, and one include that proves each."""
    dependencies: dict[str, set[str]] = {module: set() for module in modules}
    evidence: dict[tuple[str, str], str] = {}
    for file in files:
        if file.module not in dependencies or API_HEADER.match(file.path):
            continue
        for dependency, header in INCLUDE.findall(file.text):
            if dependency in dependencies and dependency != file.module:
                dependencies[file.module].add(dependency)
                evidence.setdefault(
                    (file.module, dependency), f"{file.path} -> VoltMod/{dependency}/{header}"
                )
    return dependencies, evidence


def check_composition_root(files: Iterable[SourceFile], modules: set[str]) -> list[CheckResult]:
    headers, sources = [], []
    for file in files:
        if file.module not in modules or file.module in COMPOSITION_ROOT_MODULES:
            continue
        if not (found := COMPOSITION_ROOT_INCLUDE.search(file.text)):
            continue
        message = f"{file.path} includes VoltMod/{found.group(1)}.hpp"
        if file.path.startswith("include/VoltMod/") and file.path.endswith(".hpp"):
            headers.append(CheckResult(message, hint="Only App may include the composition root."))
        elif file.path.startswith("src/") and file.path.endswith(".cpp"):
            sources.append(CheckResult(message, hint="Inject the narrower service instead."))
    return sorted(headers, key=lambda r: r.message) + sorted(sources, key=lambda r: r.message)


def check_host_boundary(files: Iterable[SourceFile]) -> list[CheckResult]:
    """Includes a host boundary header may not have; the closed set keeps std:: out of it."""
    results = []
    for file in files:
        if not (file.path.startswith(HOST_BOUNDARY_PATH) and file.path.endswith(".hpp")):
            continue
        for number, line in enumerate(file.text.splitlines(), 1):
            if not (found := ANY_INCLUDE.match(line)):
                continue
            included = found.group(1)
            if included in HOST_BOUNDARY_INCLUDES or included.startswith("<VoltMod/Host/"):
                continue
            results.append(CheckResult(
                f"{file.path}:{number}: includes {included}",
                hint="The boundary carries plain data only: <cstddef>, <cstdint>, "
                     "VoltMod/Host/ and VoltMod/Engine/EngineTypes.hpp.",
            ))
    return results


def check_conventions(
    files: Iterable[SourceFile], declaration_headers: frozenset[str] | None = None
) -> list[CheckResult]:
    """Stray forward declarations, anonymous namespaces, using-directives, and Core using the SDK.

    @p declaration_headers may forward-declare; None accepts any `*Types.hpp`.
    """
    forwards, anonymous, directives, engine = [], [], [], []
    for file in files:
        is_header = file.path.endswith(".hpp")
        if declaration_headers is None:
            declares_types = bool(PLUGIN_DECLARATION_HEADER.search(file.path))
        else:
            declares_types = file.path in declaration_headers
        is_core = file.path.startswith(CORE_PATHS)

        for number, line in enumerate(file.text.splitlines(), 1):
            where = f"{file.path}:{number}"
            declared = FORWARD_DECLARATION.match(line)
            if is_header and not declares_types and declared:
                definition = DEFINITION.format(re.escape(declared.group(1)))
                if not re.search(definition, file.text, re.MULTILINE):
                    forwards.append(CheckResult(
                        f"{where}: forward declaration `{line.strip()}`",
                        hint="Include the defining header, or use the documented *Types.hpp file.",
                    ))
            if ANONYMOUS_NAMESPACE.match(line):
                anonymous.append(CheckResult(
                    f"{where}: anonymous namespace", hint="Use a static file-scope declaration."
                ))
            if USING_DIRECTIVE.match(line):
                directives.append(CheckResult(
                    f"{where}: using-directive `{line.strip()}`",
                    hint="Qualify the name, or use a targeted using-declaration in the .cpp.",
                ))
            if is_core and (hit := ENGINE_INCLUDE.match(line)):
                engine.append(CheckResult(
                    f"{where}: Core includes {hit.group(1)}",
                    hint="Move engine-dependent code to Engine.",
                ))
    return forwards + anonymous + directives + engine


def check_framework(root: Path) -> tuple[dict[str, set[str]], list[CheckResult]]:
    """The framework's module dependencies, and every layering or convention violation."""
    include_root = root / "include/VoltMod"
    if not include_root.is_dir():
        raise VoltmodError(f"no include/VoltMod under {root.resolve()}")

    modules = sorted(path.name for path in include_root.iterdir() if path.is_dir())
    files = list(read_sources(root, FRAMEWORK_SOURCE_DIRS))
    dependencies, evidence = module_dependencies(files, modules)

    listed = set(ALLOWED_DEPENDENCIES)
    unlisted = sorted(set(modules) - listed)
    removed = sorted(listed - set(modules))
    if unlisted or removed:
        messages = [f"module {name}/ is missing from ALLOWED_DEPENDENCIES" for name in unlisted]
        messages += [f"ALLOWED_DEPENDENCIES lists {name}, which is gone" for name in removed]
        return dependencies, [CheckResult(message) for message in messages]

    results = [
        CheckResult(
            f"{owner} -> {dependency} is not allowed "
            f"(allowed: {' '.join(sorted(ALLOWED_DEPENDENCIES[owner])) or 'nothing'})\n"
            f"      {evidence[(owner, dependency)]}"
        )
        for owner, used in sorted(dependencies.items())
        for dependency in sorted(used - ALLOWED_DEPENDENCIES[owner])
    ]
    results += check_composition_root(files, listed)
    results += check_conventions(files, FRAMEWORK_DECLARATION_HEADERS)
    results += check_host_boundary(files)
    return dependencies, results


def check_plugins(root: Path) -> list[CheckResult]:
    """Source conventions in a consumer's plugins directory, or in @p root itself."""
    plugins = root / "plugins" if (root / "plugins").is_dir() else root
    if not plugins.is_dir():
        raise VoltmodError(f"no plugins directory under {root.resolve()}")
    base = plugins.relative_to(root).as_posix() if plugins != root else ""
    return check_conventions(read_sources(root, (base,)))
