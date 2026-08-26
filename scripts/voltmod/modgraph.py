"""Enforce the declared include graph between VoltMod modules."""

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
    "Hooks": {"Core", "Engine", "Entities", "Events", "Players"},
    "Commands": {"Core", "Engine", "Entities", "Players", "Messaging"},
    "Menu": {"Core", "Engine", "Entities", "Players", "Messaging", "Hooks"},
    "Http": {"Core"},
    "Database": {"Core"},
    "Unsafe": {"Core", "Engine"},
    "App": {"Core", "Engine", "Entities", "Events", "Messaging", "Players", "Hooks",
            "Commands", "Menu", "Http", "Database", "Unsafe"},
}

INCLUDE = re.compile(r'#\s*include\s*[<"]VoltMod/([A-Za-z0-9_]+)/([^>"]+)[>"]')

# Root headers have no module directory and need a separate check.
ROOT_HEADERS = re.compile(r'#\s*include\s*[<"]VoltMod/(Runtime|Api)\.hpp[>"]')

# App is the composition root, so its headers may include it.
ROOT_HEADER_EXEMPT = {"App"}

# Modules whose sources may name the runtime. App composes it; the three service modules that
# dispatch through several siblings at once take it by reference. Everything else is injected
# with the sibling services it actually uses.
ROOT_SOURCE_EXEMPT = {"App", "Players", "Commands", "Menu"}


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
    if not found and not rooted and not sourced:
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
    return 1
