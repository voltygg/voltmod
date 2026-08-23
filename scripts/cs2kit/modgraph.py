"""Check that cs2-kit's module graph is a DAG.

A CS2Kit/<Module>/ may only include modules below it; App, the composition root, is the
only one allowed to reach everything. A cycle means the Conan components can no longer
be declared, so this exits non-zero and names the include that caused it.

Usage: cs2kit-modgraph [repo-root]   (default: the working directory)
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

INCLUDE = re.compile(r'#\s*include\s*[<"]CS2Kit/([A-Za-z0-9_]+)/([^>"]+)[>"]')


def scan(root: Path):
    """Return (modules, edges, witness): who depends on whom, and one include proving it."""
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


def cycles(modules, edges):
    """Every distinct cycle, as a list of module names ending where it started."""
    found = []

    def walk(node, stack, seen):
        for nxt in sorted(edges[node]):
            if nxt in stack:
                found.append(stack[stack.index(nxt):] + [nxt])
            elif nxt not in seen:
                seen.add(nxt)
                walk(nxt, stack + [nxt], seen)

    for m in modules:
        walk(m, [m], {m})
    return {tuple(sorted(set(c))): c for c in found}.values()


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    if not (root / "include/CS2Kit").is_dir():
        print(f"error: no include/CS2Kit under {root.resolve()}")
        return 2

    modules, edges, witness = scan(root)
    for m in modules:
        print(f"{m:10} -> {' '.join(sorted(edges[m])) or '(none)'}")

    found = list(cycles(modules, edges))
    if not found:
        print("\nDAG.")
        return 0

    print(f"\n{len(found)} cycle(s):")
    for c in found:
        print("  " + " -> ".join(c))
        for a, b in zip(c, c[1:]):
            print(f"      {a}->{b}: {witness.get((a, b), '?')}")
    return 1
