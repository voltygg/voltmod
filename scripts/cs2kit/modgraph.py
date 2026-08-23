"""Check cs2-kit's module layering against the map declared here.

A CS2Kit/<Module>/ may only include the modules ALLOWED lists for it. A cycle-only
check is not enough: an upward edge (Core reaching into Sdk, say) stays acyclic and
would slip through, yet it is exactly what breaks the layering. So the map below is
the authority, and this exits non-zero naming the include that violated it.

Detail/ is the composition root's private bridge - every module may reach it, and it
reaches back into all of them. That is the one deliberate exemption.

Usage: cs2kit-modgraph [repo-root]   (default: the working directory)
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

# What each module may include. Transitive edges are spelled out, so this doubles as
# the documentation of the layering and mirrors what CMake links.
ALLOWED: dict[str, set[str]] = {
    "Core": set(),
    "Utils": {"Core"},
    "Http": {"Core", "Utils"},
    "Sdk": {"Core", "Utils"},
    "Players": {"Core", "Utils", "Sdk"},
    "Commands": {"Core", "Utils", "Sdk", "Players"},
    "Menu": {"Core", "Utils", "Sdk", "Players"},
    "Database": {"Core", "Utils"},
    "App": {"Core", "Utils", "Http", "Sdk", "Players", "Commands", "Menu", "Database"},
}

# Reachable from anywhere and reaching anywhere; see the module docstring.
EXEMPT = {"Detail"}

INCLUDE = re.compile(r'#\s*include\s*[<"]CS2Kit/([A-Za-z0-9_]+)/([^>"]+)[>"]')


def scan(root: Path):
    """Return (modules, edges, witness): who includes whom, and one include proving it."""
    modules = sorted(p.name for p in (root / "include/CS2Kit").iterdir() if p.is_dir())
    edges: defaultdict[str, set[str]] = defaultdict(set)
    witness: dict[tuple[str, str], str] = {}

    for base in ("include/CS2Kit", "src"):
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
                    witness.setdefault((owner, dep), f"{rel} -> CS2Kit/{dep}/{header}")
    return modules, edges, witness


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


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    if not (root / "include/CS2Kit").is_dir():
        print(f"error: no include/CS2Kit under {root.resolve()}")
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
    if not found:
        print("\nLayering holds.")
        return 0

    print(f"\n{len(found)} forbidden edge(s):")
    for owner, dep in found:
        print(f"  {owner} -> {dep} (allowed: {' '.join(sorted(ALLOWED[owner])) or 'nothing'})")
        print(f"      {witness.get((owner, dep), '?')}")
    return 1
