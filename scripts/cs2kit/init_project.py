#!/usr/bin/env python3
"""Stamp a complete, buildable plugin project that consumes cs2-kit as a package.

Run from an empty repo root:

    mkdir my-cs2-plugins && cd my-cs2-plugins
    git init
    uvx --from git+https://github.com/voltygg/cs2-kit.git cs2kit-init-project --plugin my-plugin
    uv sync
    uv run poe bootstrap

Generates the root CMakeLists.txt, CMakePresets.json, conanfile.py, pyproject.toml,
.gitignore, .clang-format and README.md from templates/project/, rendered with
string.Template.safe_substitute ($project only, so CMake/preset `${...}` syntax
passes through). The first plugin is then scaffolded via new_plugin.
"""

import argparse
from pathlib import Path

from . import new_plugin
from .buildtools import templates_dir

REPO_ROOT = Path.cwd()


def main() -> int:
    parser = argparse.ArgumentParser(description="Initialize a cs2-kit plugin project.")
    parser.add_argument(
        "--name",
        type=new_plugin.kebab_case,
        default=REPO_ROOT.name,
        help="kebab-case project name (default: current directory name)",
    )
    parser.add_argument(
        "--plugin",
        type=new_plugin.kebab_case,
        default="my-plugin",
        help="kebab-case name for the first plugin (default: my-plugin)",
    )
    args = parser.parse_args()

    template_dir = templates_dir() / "project"
    if not template_dir.is_dir():
        print(f"error: template tree missing at {template_dir}.")
        return 1

    for existing in ("CMakeLists.txt", "conanfile.py"):
        if (REPO_ROOT / existing).exists():
            print(f"error: {existing} already exists in {REPO_ROOT}; refusing to overwrite.")
            return 1

    plugin_dir = REPO_ROOT / "plugins" / args.plugin
    if plugin_dir.exists():
        print(f"error: {plugin_dir} already exists; refusing to overwrite.")
        return 1

    new_plugin.render_tree(template_dir, REPO_ROOT, {"project": args.name}, safe=True)
    if (code := new_plugin.scaffold_plugin(args.plugin)) != 0:
        return code

    print(
        "\nDone. Next steps:\n"
        "  uv sync              # provision the toolchain (https://docs.astral.sh/uv)\n"
        "  uv run poe bootstrap # Conan profiles + remote, then a first build"
    )
    return 0
