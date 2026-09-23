"""The framework's module layering, and the source rules only the framework follows."""

import re
from collections.abc import Iterable
from pathlib import Path

from voltmod.check_results import CheckResult
from voltmod.conventions import SourceFile, check_conventions, read_sources
from voltmod.errors import VoltmodError
from voltmod.framework.paths import INCLUDE_ROOT, SOURCE_DIRS

# Transitive dependencies are listed too: this table is the layering.
# fmt: off
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
    # Host: a command name belongs to one plugin process-wide, so registration claims it.
    "Commands": {"Core", "Engine", "Entities", "Players", "Messaging", "Host"},
    "Menu": {"Core", "Engine", "Entities", "Players", "Messaging", "Hooks", "Ui", "Workshop"},
    "Http": {"Core"},
    "Database": {"Core"},
    "Unsafe": {"Core", "Engine"},
    # Host installs for every plugin the engine hooks each used to install for itself.
    "Host": {"Core", "Engine", "Unsafe"},
    "App": {
        "Core", "Engine", "Schema", "Entities", "Events", "Messaging", "Players", "Hooks",
        "Ui", "Workshop", "Commands", "Menu", "Http", "Database", "Unsafe", "Host",
    },
}
# fmt: on

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
DECLARATION_HEADERS = frozenset({"include/VoltMod/Engine/EngineTypes.hpp"})

# Host and plugins have separate allocators: a boundary signature takes views, never owning types.
HOST_BOUNDARY_PATH = "include/VoltMod/Host/"
# fmt: off
HOST_BOUNDARY_INCLUDES = frozenset({
    "<cstddef>",
    "<cstdint>",
    "<string_view>",
    "<VoltMod/Engine/EngineTypes.hpp>",
    "<VoltMod/Engine/GameData/GameDataLocation.hpp>",
})
# fmt: on
ANY_INCLUDE = re.compile(r'^\s*#\s*include\s*([<"][^>"]+[>"])')


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


def check_core_isolation(files: Iterable[SourceFile]) -> list[CheckResult]:
    """Core includes of the SDK; Core must build without it."""
    results = []
    for file in files:
        if not file.path.startswith(CORE_PATHS):
            continue
        for number, line in enumerate(file.text.splitlines(), 1):
            if hit := ENGINE_INCLUDE.match(line):
                results.append(
                    CheckResult(
                        f"{file.path}:{number}: Core includes {hit.group(1)}",
                        hint="Move engine-dependent code to Engine.",
                    )
                )
    return results


def check_host_boundary(files: Iterable[SourceFile]) -> list[CheckResult]:
    """Includes a host boundary header may not have; the closed set keeps owning types out of it."""
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

            results.append(
                CheckResult(
                    f"{file.path}:{number}: includes {included}",
                    hint="The boundary carries plain data and borrowed views only: "
                    + ", ".join(sorted(HOST_BOUNDARY_INCLUDES))
                    + " and VoltMod/Host/.",
                )
            )
    return results


def check_framework(root: Path) -> tuple[dict[str, set[str]], list[CheckResult]]:
    """The framework's module dependencies, and every layering or convention violation."""
    include_root = root / INCLUDE_ROOT
    if not include_root.is_dir():
        raise VoltmodError(f"no {INCLUDE_ROOT.as_posix()} under {root.resolve()}")

    modules = sorted(path.name for path in include_root.iterdir() if path.is_dir())
    files = list(read_sources(root, SOURCE_DIRS))
    dependencies, evidence = module_dependencies(files, modules)

    listed = set(ALLOWED_DEPENDENCIES)
    results = [
        CheckResult(f"module {name}/ is missing from ALLOWED_DEPENDENCIES")
        for name in sorted(set(modules) - listed)
    ]
    results += [
        CheckResult(f"ALLOWED_DEPENDENCIES lists {name}, which is gone")
        for name in sorted(listed - set(modules))
    ]
    # Only a listed module has an allowed set to compare its includes against.
    results += [
        CheckResult(
            f"{owner} -> {dependency} is not allowed "
            f"(allowed: {' '.join(sorted(ALLOWED_DEPENDENCIES[owner])) or 'nothing'})\n"
            f"      {evidence[(owner, dependency)]}"
        )
        for owner, used in sorted(dependencies.items())
        if owner in listed
        for dependency in sorted(used - ALLOWED_DEPENDENCIES[owner])
    ]
    results += check_composition_root(files, listed)
    results += check_conventions(files, DECLARATION_HEADERS)
    results += check_core_isolation(files)
    results += check_host_boundary(files)
    return dependencies, results
