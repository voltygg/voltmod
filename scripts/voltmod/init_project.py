"""Scaffold a complete VoltMod consumer project in the current directory."""

from pathlib import Path

from . import new_plugin
from .buildtools import templates_dir

REPO_ROOT = Path.cwd()


def create(name: str, plugin: str) -> int:
    """Render templates/project into the working directory, then its first plugin."""
    template_dir = templates_dir() / "project"
    if not template_dir.is_dir():
        print(f"error: template tree missing at {template_dir}.")
        return 1

    for existing in ("CMakeLists.txt", "conanfile.py"):
        if (REPO_ROOT / existing).exists():
            print(f"error: {existing} already exists in {REPO_ROOT}; refusing to overwrite.")
            return 1

    plugin_dir = REPO_ROOT / "plugins" / plugin
    if plugin_dir.exists():
        print(f"error: {plugin_dir} already exists; refusing to overwrite.")
        return 1

    new_plugin.render_tree(template_dir, REPO_ROOT, {"project": name})
    if (code := new_plugin.scaffold_plugin(plugin)) != 0:
        return code

    print(
        "\nDone. Next steps:\n"
        "  uv sync              # provision the toolchain (https://docs.astral.sh/uv)\n"
        "  uv run poe bootstrap # Conan profiles + remote, then a first build"
    )
    return 0
