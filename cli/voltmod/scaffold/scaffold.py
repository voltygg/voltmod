"""Stamping a new consumer project or plugin out of the bundled templates."""

import re
import string
from pathlib import Path

from voltmod import console
from voltmod.bundled import TEMPLATES_DIR
from voltmod.errors import VoltmodError

_KEBAB_CASE = re.compile(r"^[a-z][a-z0-9]*(-[a-z0-9]+)*$")


def is_kebab_case(name: str) -> bool:
    return bool(_KEBAB_CASE.match(name))


def create_project(root: Path, name: str, plugin: str) -> None:
    """Render templates/project into `root`, then its first plugin."""
    for existing in ("CMakeLists.txt", "conanfile.py"):
        if (root / existing).exists():
            raise VoltmodError(f"{existing} already exists in {root}; refusing to overwrite")
    if (root / "plugins" / plugin).exists():
        raise VoltmodError(f"{root / 'plugins' / plugin} already exists; refusing to overwrite")

    _render_tree(_template_dir("project"), root, {"project": name})
    _add_plugin(root, plugin)

    console.done("Done. Next steps:")
    console.info("  uv sync              # provision the toolchain (https://docs.astral.sh/uv)")
    console.info("  uv run poe doctor    # check tools and project configuration")
    console.info("  uv run poe bootstrap # Conan profiles + remote, then a first build")


def create_plugin(root: Path, name: str) -> None:
    """Render templates/plugin into plugins/<name>/ and register it in CMakeLists.txt."""
    if not (root / "CMakeLists.txt").is_file():
        raise VoltmodError(f"no CMakeLists.txt in {root}; run from your repo's root")
    _add_plugin(root, name)
    console.done("Done. Build it with: uv run poe build")


def plugin_fields(name: str) -> dict[str, str]:
    """A kebab-case plugin name spelled for each template placeholder."""
    words = [word.capitalize() for word in name.split("-")]
    pascal = "".join(words)
    return {
        "name": name,
        "namespace": pascal,
        "plugin_class": f"{pascal}Plugin",
        "title": " ".join(words),
        "tag": pascal.upper()[:12],
    }


def _template_dir(kind: str) -> Path:
    path = TEMPLATES_DIR / kind
    if not path.is_dir():
        raise VoltmodError(f"template tree missing at {path}")
    return path


def _add_plugin(root: Path, name: str) -> None:
    plugin_dir = root / "plugins" / name
    if plugin_dir.exists():
        raise VoltmodError(f"{plugin_dir} already exists; refusing to overwrite")

    _render_tree(_template_dir("plugin"), plugin_dir, plugin_fields(name), label=f"plugins/{name}/")
    if _register_subdirectory(root / "CMakeLists.txt", name):
        console.item(f"registered add_subdirectory(plugins/{name}) in CMakeLists.txt")


def _render_tree(template_dir: Path, target: Path, fields: dict[str, str], label: str = "") -> None:
    # safe_substitute leaves placeholders the templates mean for runtime untouched.
    for template in sorted(template_dir.rglob("*")):
        if not template.is_file():
            continue
        relative = template.relative_to(template_dir)
        text = string.Template(template.read_text(encoding="utf-8")).safe_substitute(fields)
        out = target / relative
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8", newline="\n")
        console.item(f"created {label}{relative.as_posix()}")


def _register_subdirectory(root_cmake: Path, name: str) -> bool:
    """Add add_subdirectory(plugins/<name>) after the last plugin one; False if already there."""
    line = f"add_subdirectory(plugins/{name})"
    text = root_cmake.read_text(encoding="utf-8")
    if line in text:
        return False

    lines = text.splitlines(keepends=True)
    registered = [
        index
        for index, entry in enumerate(lines)
        if entry.strip().startswith("add_subdirectory(plugins/")
    ]
    last = registered[-1] if registered else len(lines) - 1
    lines.insert(last + 1, line + "\n")
    root_cmake.write_text("".join(lines), encoding="utf-8", newline="\n")
    return True
