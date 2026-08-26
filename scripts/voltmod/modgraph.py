"""Enforce the declared include graph and the source conventions VoltMod relies on."""

import re
from collections import defaultdict
from pathlib import Path

# Transitive edges are explicit so this also documents the layering.
ALLOWED: dict[str, set[str]] = {
    "Core": set(),
    "Engine": {"Core"},
    "Entities": {"Core", "Engine"},
    "Events": {"Core", "Engine", "Entities"},
    "Messaging": {"Core", "Engine", "Entities", "Events"},
    "Players": {"Core", "Engine", "Entities"},
    # Messaging: HookServices (Hooks/Hooks.hpp) owns Vote, whose header lives in Messaging/ -
    # Runtime's grouping puts the game's own vote panel beside the hooks that are dormant the
    # same way, not beside Messages/CenterHtml.
    "Hooks": {"Core", "Engine", "Entities", "Events", "Players", "Unsafe", "Messaging"},
    "Commands": {"Core", "Engine", "Entities", "Players", "Messaging"},
    "Menu": {"Core", "Engine", "Entities", "Players", "Messaging", "Hooks"},
    "Http": {"Core"},
    "Database": {"Core"},
    "Unsafe": {"Core", "Engine"},
    "App": {"Core", "Engine", "Entities", "Events", "Messaging", "Players", "Hooks",
            "Commands", "Menu", "Http", "Database", "Unsafe"},
}

INCLUDE = re.compile(r'#\s*include\s*[<"]VoltMod/([A-Za-z0-9_]+)/([^>"]+)[>"]')

# A module's own Api.hpp (Entities/Api.hpp, Hooks/Api.hpp, ...) is a deliberate cross-module
# aggregate - Hooks/Api.hpp, for instance, gathers the Events and Messaging types a hook
# handler needs alongside the hooks themselves. Its includes document that module's public
# surface, not a source-level dependency, so they are not edges and are exempt from the
# layering check below.
AGGREGATE_HEADER = re.compile(r'^include/VoltMod/[A-Za-z0-9_]+/Api\.hpp$')

# Root headers have no module directory and need a separate check.
ROOT_HEADERS = re.compile(r'#\s*include\s*[<"]VoltMod/(Runtime|Api)\.hpp[>"]')

# App is the composition root, so its headers may include it.
ROOT_HEADER_EXEMPT = {"App"}

# These modules sit below the composition root; a header here reaching into Menu (the
# opt-in UI module) or App (which may reach Runtime) would make every consumer of that
# layer pull in menu building, or the runtime, whether it wants to or not.
NO_MENU_OR_APP = {"Core", "Engine", "Entities", "Events", "Messaging", "Players", "Hooks",
                  "Commands"}
CROSS_MODULE_HEADER = re.compile(r'#\s*include\s*[<"]VoltMod/(Menu|App)/')

# nlohmann is a real dependency (parsing, (de)serializing), declared explicitly by the few
# files that own it, not picked up incidentally by including one of them. Http/RestJsonApi and
# Engine/GameDataFile are listed as header+source pairs; everything else routes through
# <VoltMod/Core/Json.hpp>, which is itself allowed to name nlohmann.
NLOHMANN_INCLUDE = re.compile(r'#\s*include\s*[<"]nlohmann/')
NLOHMANN_ALLOWED = {
    "include/VoltMod/Core/Json.hpp",
    "include/VoltMod/App/Config.hpp",
    "include/VoltMod/App/JsonConfig.hpp",
    "include/VoltMod/App/PluginSettings.hpp",
    "include/VoltMod/Http/RestJsonApi.hpp",
    "src/Http/RestJsonApi.cpp",
    "src/Engine/GameDataFile.hpp",
    "src/Engine/GameDataFile.cpp",
}

# Core is the engine-free layer: nothing under it may reach the SDK or Metamod.
ENGINE_FREE = ("include/VoltMod/Core", "src/Core")
ENGINE_INCLUDE = re.compile(
    r'#\s*include\s*[<"](ISmmPlugin\.h|tier0/|eiface\.h|entity2/|schemasystem/|icvar\.h|Color\.h)')

# Forward declarations live in one header per module graph, named `*Types.hpp`, which says why
# each name is there: <VoltMod/Engine/EngineTypes.hpp> for the framework. A class pair that owns
# one another (Runtime and the managers it holds by value) cannot both include the other's
# header, and that is the only reason a name belongs in one. Every other header includes what it
# names.
FORWARD_DECL_HOME = re.compile(r'(^|/)\w*Types\.hpp$')
FORWARD_DECL = re.compile(r'^(?:class|struct)\s+(\w+);')

# A header that declares a name it goes on to define is ordering its own contents, not standing in
# for an include: a primary template declared before its partial specializations, or a pair inside
# one header where each needs the other's name.
DEFINITION = r'^(?:class|struct)\s+{}\b\s*(?!;)'

# A file-local helper is a `static` declaration, not an anonymous namespace: `static` says
# "file-local" on the declaration itself, where a reader of the line can see it.
ANON_NAMESPACE = re.compile(r'^[ \t]*namespace[ \t]*(\{[ \t]*)?$')

# Using-directives make an unqualified name's origin invisible. Targeted using-declarations
# (`using VoltMod::Player;`) in a .cpp are fine and are not matched here.
USING_DIRECTIVE = re.compile(r'^[ \t]*using\s+namespace\b')

# Modules whose sources may name the runtime. App composes it; everything else is injected with
# the sibling services it actually uses.
ROOT_SOURCE_EXEMPT = {"App"}


def scan(root: Path):
    """Return (modules, edges, witness): who includes whom, and one include proving it."""
    modules = sorted(p.name for p in (root / "include/VoltMod").iterdir() if p.is_dir())
    edges: defaultdict[str, set[str]] = defaultdict(set)
    witness: dict[tuple[str, str], str] = {}

    for base in ("include/VoltMod", "src"):
        for path in (root / base).rglob("*"):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            parts = path.relative_to(root / base).parts
            owner = parts[0] if len(parts) > 1 else None
            if owner not in modules:
                continue
            rel = path.relative_to(root).as_posix()
            if AGGREGATE_HEADER.match(rel):
                continue
            for dep, header in INCLUDE.findall(path.read_text(encoding="utf-8", errors="replace")):
                if dep in modules and dep != owner:
                    edges[owner].add(dep)
                    witness.setdefault((owner, dep), f"{rel} -> VoltMod/{dep}/{header}")
    return modules, edges, witness


def root_in_headers(root: Path, modules):
    """Headers below the composition root that include VoltMod/Runtime.hpp or Api.hpp."""
    found = []
    for path in (root / "include/VoltMod").rglob("*.hpp"):
        parts = path.relative_to(root / "include/VoltMod").parts
        owner = parts[0] if len(parts) > 1 else None
        if owner not in modules or owner in ROOT_HEADER_EXEMPT:
            continue
        hit = ROOT_HEADERS.search(path.read_text(encoding="utf-8", errors="replace"))
        if hit:
            found.append((path.relative_to(root).as_posix(), hit.group(1)))
    return sorted(found)


def root_in_sources(root: Path, modules):
    """Sources below the composition root that include VoltMod/Runtime.hpp or Api.hpp.

    Api.hpp pulls Runtime.hpp in, so both count. src/Runtime.cpp itself has no module
    directory and is skipped like any other root file.
    """
    found = []
    for path in (root / "src").rglob("*.cpp"):
        parts = path.relative_to(root / "src").parts
        owner = parts[0] if len(parts) > 1 else None
        if owner not in modules or owner in ROOT_SOURCE_EXEMPT:
            continue
        hit = ROOT_HEADERS.search(path.read_text(encoding="utf-8", errors="replace"))
        if hit:
            found.append((path.relative_to(root).as_posix(), hit.group(1)))
    return sorted(found)


def forbidden_surfaces(root: Path, modules):
    """Menu/App reached from a layer below the composition root, and nlohmann included
    outside its allowlist - across include/VoltMod and src, aggregate headers included:
    an aggregate documents its own module's surface, not its neighbors'."""
    crossed, nlohmann = [], []
    for base in ("include/VoltMod", "src"):
        for path in (root / base).rglob("*"):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            parts = path.relative_to(root / base).parts
            owner = parts[0] if len(parts) > 1 else None
            rel = path.relative_to(root).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")

            if owner in NO_MENU_OR_APP:
                for number, line in enumerate(text.splitlines(), 1):
                    hit = CROSS_MODULE_HEADER.search(line)
                    if hit:
                        crossed.append((rel, number, hit.group(1)))

            if rel not in NLOHMANN_ALLOWED:
                for number, line in enumerate(text.splitlines(), 1):
                    if NLOHMANN_INCLUDE.search(line):
                        nlohmann.append((rel, number))
    return sorted(crossed), sorted(nlohmann)


def sources(root: Path, bases):
    """Every .hpp/.cpp under the given bases, as (relative posix path, text)."""
    for base in bases:
        d = root / base
        if not d.is_dir():
            continue
        for path in sorted(d.rglob("*")):
            if path.suffix in (".hpp", ".cpp"):
                yield path.relative_to(root).as_posix(), path.read_text(encoding="utf-8",
                                                                        errors="replace")


def conventions(root: Path, bases):
    """Forward declarations, anonymous namespaces and using-directives, with line numbers."""
    forwards, anonymous, directives = [], [], []
    for rel, text in sources(root, bases):
        header = rel.endswith(".hpp")
        home = bool(FORWARD_DECL_HOME.search(rel))
        for number, line in enumerate(text.splitlines(), 1):
            declared = FORWARD_DECL.match(line)
            if header and not home and declared:
                pattern = DEFINITION.format(re.escape(declared.group(1)))
                if not re.search(pattern, text, re.MULTILINE):
                    forwards.append((rel, number, line.strip()))
            if ANON_NAMESPACE.match(line):
                anonymous.append((rel, number))
            if USING_DIRECTIVE.match(line):
                directives.append((rel, number, line.strip()))
    return forwards, anonymous, directives


def engine_in_core(root: Path):
    """Core translation units that reach the SDK or Metamod."""
    found = []
    for rel, text in sources(root, ENGINE_FREE):
        for number, line in enumerate(text.splitlines(), 1):
            hit = ENGINE_INCLUDE.match(line)
            if hit:
                found.append((rel, number, hit.group(1)))
    return found


def report_conventions(root: Path, bases, label: str) -> int:
    """Print every convention breach under `bases`; return a process exit code."""
    forwards, anonymous, directives = conventions(root, bases)
    if not (forwards or anonymous or directives):
        return 0

    if forwards:
        print(f"\n{len(forwards)} forward declaration(s) in {label} headers:")
        for rel, number, line in forwards:
            print(f"  {rel}:{number}: {line}")
        print("      Include the header that defines the type. A forward declaration belongs in")
        print("      the one *Types.hpp header, which says why each name is there.")

    if anonymous:
        print(f"\n{len(anonymous)} anonymous namespace(s) in {label}:")
        for rel, number in anonymous:
            print(f"  {rel}:{number}")
        print("      Declare file-local helpers `static` at file scope, or make them private")
        print("      static members when they need class state.")

    if directives:
        print(f"\n{len(directives)} using-directive(s) in {label}:")
        for rel, number, line in directives:
            print(f"  {rel}:{number}: {line}")
        print("      Qualify the name, or add a targeted `using VoltMod::Thing;` in the .cpp.")
    return 1


def check_plugins(root: Path) -> int:
    """Run the source conventions over a consumer repository's plugins/."""
    plugins = root / "plugins" if (root / "plugins").is_dir() else root
    if not plugins.is_dir():
        print(f"error: no plugins directory under {root.resolve()}")
        return 2
    base = plugins.relative_to(root).as_posix() if plugins != root else "."
    bases = [base] if base != "." else [""]
    code = report_conventions(root, bases, "plugin")
    if code == 0:
        print("Plugin sources hold.")
    return code


def undeclared(modules):
    """Modules on disk that ALLOWED says nothing about, and vice versa."""
    known = set(ALLOWED)
    return sorted(set(modules) - known), sorted(known - set(modules))


def violations(edges):
    """Every (owner, dep) the map forbids."""
    found = []
    for owner, deps in sorted(edges.items()):
        found.extend((owner, dep) for dep in sorted(deps - ALLOWED.get(owner, set())))
    return found


def check(root: Path) -> int:
    """Print the graph and return a process exit code."""
    if not (root / "include/VoltMod").is_dir():
        print(f"error: no include/VoltMod under {root.resolve()}")
        return 2

    modules, edges, witness = scan(root)
    for m in modules:
        print(f"{m:10} -> {' '.join(sorted(edges[m])) or '(none)'}")

    missing, stale = undeclared(modules)
    if missing or stale:
        print()
        for m in missing:
            print(f"error: module {m}/ exists but ALLOWED does not list it")
        for m in stale:
            print(f"error: ALLOWED lists {m}, which no longer exists")
        return 1

    found = violations(edges)
    rooted = root_in_headers(root, modules)
    sourced = root_in_sources(root, modules)
    engined = engine_in_core(root)
    crossed, nlohmann_hits = forbidden_surfaces(root, modules)
    conventional = report_conventions(root, ("include/VoltMod", "src"), "framework")
    if not (found or rooted or sourced or engined or crossed or nlohmann_hits or conventional):
        print("\nLayering holds.")
        return 0

    if found:
        print(f"\n{len(found)} forbidden edge(s):")
        for owner, dep in found:
            print(f"  {owner} -> {dep} (allowed: {' '.join(sorted(ALLOWED[owner])) or 'nothing'})")
            print(f"      {witness.get((owner, dep), '?')}")

    if rooted:
        print(f"\n{len(rooted)} header(s) including the composition root:")
        for rel, header in rooted:
            print(f"  {rel} -> VoltMod/{header}.hpp")
        print("      A .cpp in App, Players, Commands or Menu may include the root; a header may")
        print("      not - it would pull every service into each consumer. Forward-declare Runtime")
        print("      (or take the one service you need) in the header and include the root from")
        print("      the .cpp.")

    if sourced:
        print(f"\n{len(sourced)} source(s) naming the composition root:")
        for rel, header in sourced:
            print(f"  {rel} -> VoltMod/{header}.hpp")
        print(f"      Only {' '.join(sorted(ROOT_SOURCE_EXEMPT))} may reach the runtime. Take the")
        print("      sibling services this file uses through its constructor instead.")

    if engined:
        print(f"\n{len(engined)} Core file(s) reaching the SDK or Metamod:")
        for rel, number, header in engined:
            print(f"  {rel}:{number}: {header}")
        print("      Core is the engine-free layer. Move the file to src/Engine/ (or take the")
        print("      engine call through a service that already lives there).")

    if crossed:
        print(f"\n{len(crossed)} file(s) below the composition root reaching Menu or App:")
        for rel, number, target in crossed:
            print(f"  {rel}:{number}: VoltMod/{target}/...")
        print(f"      {' '.join(sorted(NO_MENU_OR_APP))} sit below Menu and App; a plugin that")
        print("      never touches menus or the runtime should not pull either in by including")
        print("      this file. Take the narrower type this file actually needs instead.")

    if nlohmann_hits:
        print(f"\n{len(nlohmann_hits)} file(s) including nlohmann outside its allowlist:")
        for rel, number in nlohmann_hits:
            print(f"  {rel}:{number}")
        print("      Only Core/Json.hpp, App/Config.hpp, App/JsonConfig.hpp,")
        print("      App/PluginSettings.hpp, Http/RestJsonApi.* and Engine/GameDataFile.* declare")
        print("      nlohmann directly - route through <VoltMod/Core/Json.hpp> instead, so")
        print("      <VoltMod/Api.hpp> never has to.")
    return 1
