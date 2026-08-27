"""Check VoltMod module boundaries and source conventions."""

import re
from pathlib import Path

SOURCE_DIRS = ("include/VoltMod", "src")
SOURCE_SUFFIXES = {".hpp", ".cpp"}

# Transitive dependencies are explicit because this map defines the layering.
ALLOWED: dict[str, set[str]] = {
    "Core": set(),
    "Engine": {"Core"},
    "Entities": {"Core", "Engine"},
    "Events": {"Core", "Engine", "Entities"},
    "Messaging": {"Core", "Engine", "Entities", "Events"},
    "Players": {"Core", "Engine", "Entities"},
    "Hooks": {"Core", "Engine", "Entities", "Events", "Players", "Unsafe"},
    "Hud": {"Core", "Engine", "Entities", "Unsafe"},
    "Workshop": {"Core", "Engine", "Players", "Unsafe"},
    "Commands": {"Core", "Engine", "Entities", "Players", "Messaging"},
    "Menu": {"Core", "Engine", "Entities", "Players", "Messaging", "Hooks"},
    "Http": {"Core"},
    "Database": {"Core"},
    "Unsafe": {"Core", "Engine"},
    "App": {
        "Core",
        "Engine",
        "Entities",
        "Events",
        "Messaging",
        "Players",
        "Hooks",
        "Hud",
        "Workshop",
        "Commands",
        "Menu",
        "Http",
        "Database",
        "Unsafe",
    },
}

INCLUDE = re.compile(r'#\s*include\s*[<"]VoltMod/([A-Za-z0-9_]+)/([^>"]+)[>"]')
# Module Api.hpp files aggregate public types; their includes are not dependency edges.
AGGREGATE_HEADER = re.compile(r"^include/VoltMod/[A-Za-z0-9_]+/Api\.hpp$")

ROOT_HEADERS = re.compile(r'#\s*include\s*[<"]VoltMod/(Runtime|Api)\.hpp[>"]')
ROOT_EXEMPT = {"App"}

NLOHMANN_INCLUDE = re.compile(r'#\s*include\s*[<"]nlohmann/')
NLOHMANN_ALLOWED = {
    "include/VoltMod/Core/Json.hpp",
    "include/VoltMod/App/Config.hpp",
    "include/VoltMod/App/JsonConfig.hpp",
    "include/VoltMod/App/PluginSettings.hpp",
    "src/Engine/GameDataFile.hpp",
    "src/Engine/GameDataFile.cpp",
}

CORE_PATHS = ("include/VoltMod/Core/", "src/Core/")
ENGINE_INCLUDE = re.compile(
    r'#\s*include\s*[<"](ISmmPlugin\.h|tier0/|eiface\.h|entity2/|schemasystem/|icvar\.h|Color\.h)'
)

# The one framework header allowed to forward-declare, named rather than matched: EventTypes.hpp
# and ConVarTypes.hpp are definition headers whose names a *Types.hpp pattern would also exempt.
FRAMEWORK_DECL_HOMES = frozenset({"include/VoltMod/Engine/EngineTypes.hpp"})
# A consumer's layout is not ours to name, so its one declaration home is matched by filename.
PLUGIN_DECL_HOME = re.compile(r"(^|/)\w*Types\.hpp$")
FORWARD_DECL = re.compile(r"^(?:class|struct)\s+(\w+);")
DEFINITION = r"^(?:class|struct)\s+{}\b\s*(?!;)"
ANON_NAMESPACE = re.compile(r"^[ \t]*namespace[ \t]*(\{[ \t]*)?$")
USING_DIRECTIVE = re.compile(r"^[ \t]*using\s+namespace\b")


def layering_table() -> str:
    """@ref ALLOWED rendered as the fenced block the docs quote.

    The map is the only copy of the layering that runs; a doc that restates it by hand drifts
    silently. `scripts/tests/test_modgraph.py` asserts both copies still match this.
    """
    lines = []
    for module, allowed in ALLOWED.items():
        if module == "App":
            depends = "every module"
        else:
            depends = ", ".join(m for m in ALLOWED if m in allowed) or "nothing"
        lines.append(f"{module:<10} -> {depends}")
    return "\n".join(lines)


def source_files(root: Path, bases):
    """Yield relative path, module owner, and text for C++ sources below bases."""
    for base in bases:
        directory = root / base
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            parts = path.relative_to(directory).parts
            owner = parts[0] if len(parts) > 1 else None
            yield (
                path.relative_to(root).as_posix(),
                owner,
                path.read_text(encoding="utf-8", errors="replace"),
            )


def dependencies(files, modules):
    """Return module edges and one include that proves each edge."""
    edges = {module: set() for module in modules}
    witness = {}
    for rel, owner, text in files:
        if owner not in edges or AGGREGATE_HEADER.match(rel):
            continue
        for dependency, header in INCLUDE.findall(text):
            if dependency in edges and dependency != owner:
                edges[owner].add(dependency)
                witness.setdefault(
                    (owner, dependency), f"{rel} -> VoltMod/{dependency}/{header}"
                )
    return edges, witness


def root_includes(files, modules):
    """Find framework headers and sources that include the composition root."""
    headers, sources = [], []
    for rel, owner, text in files:
        if owner not in modules or owner in ROOT_EXEMPT:
            continue
        hit = ROOT_HEADERS.search(text)
        if not hit:
            continue
        item = (rel, hit.group(1))
        if rel.startswith("include/VoltMod/") and rel.endswith(".hpp"):
            headers.append(item)
        elif rel.startswith("src/") and rel.endswith(".cpp"):
            sources.append(item)
    return sorted(headers), sorted(sources)


def scan(files, decl_homes=None):
    """Walk every source line once and collect the line-based violations.

    @p decl_homes is the set of relative paths allowed to forward-declare, or None to accept any
    `*Types.hpp` - which is all a consumer's arbitrary layout lets us say.
    """
    found = {"nlohmann": [], "forwards": [], "anonymous": [], "directives": [], "engine": []}
    for rel, _, text in files:
        header = rel.endswith(".hpp")
        if decl_homes is None:
            declaration_home = bool(PLUGIN_DECL_HOME.search(rel))
        else:
            declaration_home = rel in decl_homes
        core = rel.startswith(CORE_PATHS)
        allows_nlohmann = rel in NLOHMANN_ALLOWED

        for number, line in enumerate(text.splitlines(), 1):
            if not allows_nlohmann and NLOHMANN_INCLUDE.search(line):
                found["nlohmann"].append((rel, number))
            declared = FORWARD_DECL.match(line)
            if header and not declaration_home and declared:
                pattern = DEFINITION.format(re.escape(declared.group(1)))
                if not re.search(pattern, text, re.MULTILINE):
                    found["forwards"].append((rel, number, line.strip()))
            if ANON_NAMESPACE.match(line):
                found["anonymous"].append((rel, number))
            if USING_DIRECTIVE.match(line):
                found["directives"].append((rel, number, line.strip()))
            if core:
                hit = ENGINE_INCLUDE.match(line)
                if hit:
                    found["engine"].append((rel, number, hit.group(1)))
    return found


def report_conventions(found, label: str) -> int:
    """Print source-convention violations from a @ref scan result."""
    forwards, anonymous, directives = found["forwards"], found["anonymous"], found["directives"]
    if not (forwards or anonymous or directives):
        return 0

    if forwards:
        print(f"\n{len(forwards)} forward declaration(s) in {label} headers:")
        for rel, number, line in forwards:
            print(f"  {rel}:{number}: {line}")
        print("      Include the defining header or use the documented *Types.hpp file.")

    if anonymous:
        print(f"\n{len(anonymous)} anonymous namespace(s) in {label}:")
        for rel, number in anonymous:
            print(f"  {rel}:{number}")
        print("      Use a static file-scope declaration.")

    if directives:
        print(f"\n{len(directives)} using-directive(s) in {label}:")
        for rel, number, line in directives:
            print(f"  {rel}:{number}: {line}")
        print("      Qualify the name or use a targeted using-declaration in the .cpp.")
    return 1


def check_plugins(root: Path) -> int:
    """Check source conventions in a consumer's plugins directory."""
    plugins = root / "plugins" if (root / "plugins").is_dir() else root
    if not plugins.is_dir():
        print(f"error: no plugins directory under {root.resolve()}")
        return 2

    base = plugins.relative_to(root).as_posix() if plugins != root else ""
    code = report_conventions(scan(source_files(root, (base,))), "plugin")
    if code == 0:
        print("Plugin sources hold.")
    return code


def check(root: Path) -> int:
    """Print the module graph and report policy violations."""
    include_root = root / "include/VoltMod"
    if not include_root.is_dir():
        print(f"error: no include/VoltMod under {root.resolve()}")
        return 2

    modules = sorted(path.name for path in include_root.iterdir() if path.is_dir())
    files = list(source_files(root, SOURCE_DIRS))
    edges, witness = dependencies(files, modules)

    for module in modules:
        print(f"{module:10} -> {' '.join(sorted(edges[module])) or '(none)'}")

    known = set(ALLOWED)
    missing = sorted(set(modules) - known)
    stale = sorted(known - set(modules))
    if missing or stale:
        print()
        for module in missing:
            print(f"error: module {module}/ exists but ALLOWED does not list it")
        for module in stale:
            print(f"error: ALLOWED lists {module}, which no longer exists")
        return 1

    forbidden_edges = [
        (owner, dependency)
        for owner, dependencies_ in sorted(edges.items())
        for dependency in sorted(dependencies_ - ALLOWED[owner])
    ]
    rooted_headers, rooted_sources = root_includes(files, known)
    found = scan(files, FRAMEWORK_DECL_HOMES)
    engine_hits, nlohmann_hits = found["engine"], found["nlohmann"]
    convention_errors = report_conventions(found, "framework")

    if not (
        forbidden_edges
        or rooted_headers
        or rooted_sources
        or engine_hits
        or nlohmann_hits
        or convention_errors
    ):
        print("\nLayering holds.")
        return 0

    if forbidden_edges:
        print(f"\n{len(forbidden_edges)} forbidden edge(s):")
        for owner, dependency in forbidden_edges:
            allowed = " ".join(sorted(ALLOWED[owner])) or "nothing"
            print(f"  {owner} -> {dependency} (allowed: {allowed})")
            print(f"      {witness.get((owner, dependency), '?')}")

    if rooted_headers:
        print(f"\n{len(rooted_headers)} header(s) including the composition root:")
        for rel, header in rooted_headers:
            print(f"  {rel} -> VoltMod/{header}.hpp")
        print("      Only App may include the composition root.")

    if rooted_sources:
        print(f"\n{len(rooted_sources)} source(s) including the composition root:")
        for rel, header in rooted_sources:
            print(f"  {rel} -> VoltMod/{header}.hpp")
        print("      Inject the narrower service instead.")

    if engine_hits:
        print(f"\n{len(engine_hits)} Core file(s) reaching the SDK or Metamod:")
        for rel, number, header in engine_hits:
            print(f"  {rel}:{number}: {header}")
        print("      Move engine-dependent code to Engine.")

    if nlohmann_hits:
        print(f"\n{len(nlohmann_hits)} direct nlohmann include(s) outside the allowlist:")
        for rel, number in nlohmann_hits:
            print(f"  {rel}:{number}")
        print("      Include VoltMod/Core/Json.hpp instead.")
    return 1
