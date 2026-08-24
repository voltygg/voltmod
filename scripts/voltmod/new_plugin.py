"""Scaffold a buildable plugin under the current project's plugins directory."""

import argparse
import re
import string
from pathlib import Path

from .buildtools import templates_dir

REPO_ROOT = Path.cwd()
TEMPLATE_DIR = templates_dir() / "plugin"

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
        "name": name,
        "ns": pascal,
        "klass": f"{pascal}Plugin",
        "title": " ".join(parts),
        "tag": pascal.upper()[:12],
    }


def render_tree(template_dir: Path, dest: Path, subs: dict[str, str], *, label: str = "") -> None:
    """Render known template fields and preserve runtime placeholders."""
    for template in sorted(template_dir.rglob("*")):
        if not template.is_file():
            continue
        rel = template.relative_to(template_dir)
        tmpl = string.Template(template.read_text(encoding="utf-8"))
        content = tmpl.safe_substitute(subs)
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
    """Render a plugin and register its CMake subdirectory."""
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


def create(name: str) -> int:
    """Render templates/plugin into plugins/<name>/ and register the subdirectory."""
    if not (REPO_ROOT / "CMakeLists.txt").is_file():
        print(f"error: no CMakeLists.txt in {REPO_ROOT}; run from your repo's root.")
        return 1

    if (code := scaffold_plugin(name)) != 0:
        return code

    print("\nDone. Build it with: uv run poe build")
    return 0
