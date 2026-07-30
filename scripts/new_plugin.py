"""Stamp a new plugin skeleton under plugins/<name>/ in the consuming repo.

Usage (from the consuming repo's root): uv run poe new-plugin <name>

<name> is kebab-case (e.g. "fun-votes"). The generated plugin builds as-is: it loads
settings.jsonc, installs a permissive policy, and answers !ping. The repo's root
CMakeLists.txt gets its add_subdirectory() line inserted automatically.

Templates live in cs2-kit's templates/plugin/ tree, mirroring the output layout;
file contents are rendered with string.Template ($name, $ns, $klass, $title, $tag).
The script targets the current working directory, so any repo that vendors the kit
can expose it as a task, e.g.:

    new-plugin = "python vendor/cs2-kit/scripts/new_plugin.py"
"""

import argparse
import re
import string
import sys
from pathlib import Path

REPO_ROOT = Path.cwd()
TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates" / "plugin"

NAME_RE = re.compile(r"^[a-z][a-z0-9]*(-[a-z0-9]+)*$")


def kebab_case(value: str) -> str:
    """argparse type: a kebab-case name like 'fun-votes'."""
    if not NAME_RE.match(value):
        raise argparse.ArgumentTypeError(f"'{value}' is not kebab-case (expected e.g. 'fun-votes')")
    return value


def substitutions(name: str) -> dict[str, str]:
    parts = [p.capitalize() for p in name.split("-")]
    pascal = "".join(parts)
    return {
        "name": name,                # fun-votes
        "ns": pascal,                # FunVotes
        "klass": f"{pascal}Plugin",  # FunVotesPlugin
        "title": " ".join(parts),    # Fun Votes
        "tag": pascal.upper()[:12],  # FUNVOTES
    }


def render_tree(
    template_dir: Path, dest: Path, subs: dict[str, str], *, safe: bool = False, label: str = ""
) -> None:
    """Render a template tree into dest; safe_substitute leaves unknown $vars intact."""
    for template in sorted(template_dir.rglob("*")):
        if not template.is_file():
            continue
        rel = template.relative_to(template_dir)
        tmpl = string.Template(template.read_text(encoding="utf-8"))
        content = tmpl.safe_substitute(subs) if safe else tmpl.substitute(subs)
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(content, encoding="utf-8", newline="\n")
        print(f"  created {label}{rel.as_posix()}")


def insert_subdirectory(root_cmake: Path, name: str) -> bool:
    """Add add_subdirectory(plugins/<name>) after the last plugin add_subdirectory."""
    line = f"add_subdirectory(plugins/{name})"
    text = root_cmake.read_text(encoding="utf-8")
    if line in text:
        return False

    lines = text.splitlines(keepends=True)
    last = max(
        (i for i, ln in enumerate(lines) if ln.strip().startswith("add_subdirectory(plugins/")),
        default=len(lines) - 1,
    )
    lines.insert(last + 1, line + "\n")
    root_cmake.write_text("".join(lines), encoding="utf-8", newline="\n")
    return True


def scaffold_plugin(name: str) -> int:
    """Render templates/plugin into plugins/<name>/ and register the subdirectory.

    Shared by this script and init_project.py; returns a process exit code.
    """
    if not TEMPLATE_DIR.is_dir():
        print(f"error: template tree missing at {TEMPLATE_DIR}.")
        return 1

    plugin_dir = REPO_ROOT / "plugins" / name
    if plugin_dir.exists():
        print(f"error: {plugin_dir} already exists; refusing to overwrite.")
        return 1

    render_tree(TEMPLATE_DIR, plugin_dir, substitutions(name), label=f"plugins/{name}/")

    if insert_subdirectory(REPO_ROOT / "CMakeLists.txt", name):
        print(f"  registered add_subdirectory(plugins/{name}) in CMakeLists.txt")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("name", type=kebab_case, help="kebab-case plugin name, e.g. fun-votes")
    name = parser.parse_args().name

    if not (REPO_ROOT / "CMakeLists.txt").is_file():
        print(f"error: no CMakeLists.txt in {REPO_ROOT}; run from your repo's root.")
        return 1

    if (code := scaffold_plugin(name)) != 0:
        return code

    print("\nDone. Build it with: uv run poe build")
    return 0


if __name__ == "__main__":
    sys.exit(main())
