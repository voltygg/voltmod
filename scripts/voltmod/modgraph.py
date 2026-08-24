"""Enforce the declared include graph between VoltMod modules."""

import re
from collections import defaultdict
from pathlib import Path

# Transitive edges are explicit so this also documents the layering.
ALLOWED: dict[str, set[str]] = {
    "Core": set(),
    "Http": {"Core"},
    "Sdk": {"Core"},
    "Players": {"Core", "Sdk"},
    "Commands": {"Core", "Sdk", "Players"},
    "Menu": {"Core", "Sdk", "Players"},
    "Database": {"Core"},
    "App": {"Core", "Http", "Sdk", "Players", "Commands", "Menu", "Database"},
}

# Private composition bridge; every module may use it.
EXEMPT = {"Detail"}

INCLUDE = re.compile(r'#\s*include\s*[<"]VoltMod/([A-Za-z0-9_]+)/([^>"]+)[>"]')

# Root headers have no module directory and need a separate check.
ROOT_HEADERS = re.compile(r'#\s*include\s*[<"]VoltMod/(Runtime|Api)\.hpp[>"]')

# App is the composition root, so its headers may include it.
ROOT_HEADER_EXEMPT = {"App"}


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
        if owner not in modules or owner in ROOT_HEADER_EXEMPT or owner in EXEMPT:
            continue
        hit = ROOT_HEADERS.search(path.read_text(encoding="utf-8", errors="replace"))
        if hit:
            found.append((path.relative_to(root).as_posix(), hit.group(1)))
    return sorted(found)


def undeclared(modules):
    """Modules on disk that ALLOWED says nothing about, and vice versa."""
    known = set(ALLOWED) | EXEMPT
    return sorted(set(modules) - known), sorted(known - set(modules) - EXEMPT)


def violations(edges):
    """Every (owner, dep) the map forbids, ignoring the exempt modules."""
    found = []
    for owner, deps in sorted(edges.items()):
        if owner in EXEMPT:
            continue
        permitted = ALLOWED.get(owner, set()) | EXEMPT
        found.extend((owner, dep) for dep in sorted(deps - permitted))
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
    if not found and not rooted:
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
        print("      A .cpp may include the root; a header may not - it would pull every")
        print("      service into each consumer. Use the VoltMod::Detail per-service accessors.")
    return 1
