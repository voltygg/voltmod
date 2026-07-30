#!/usr/bin/env python3
"""Stamp a complete, buildable plugin project around a vendored cs2-kit.

Run from an empty repo root that vendors the kit at vendor/cs2-kit:

    mkdir my-cs2-plugins && cd my-cs2-plugins
    git init
    git submodule add https://github.com/voltygg/cs2-kit.git vendor/cs2-kit
    git submodule update --init --recursive
    python vendor/cs2-kit/scripts/init_project.py --plugin my-plugin
    uv sync
    uv run poe build

Generates the root CMakeLists.txt, CMakePresets.json (includes the kit's presets),
conanfile.py, pyproject.toml (poe tasks call the kit's scripts directly),
.gitignore, .clang-format, and README.md from templates/project/, rendered with
string.Template.safe_substitute ($project only, so CMake/preset `${...}` syntax
passes through). The first plugin is then scaffolded via new_plugin.py.
"""

import argparse
from pathlib import Path

import new_plugin

REPO_ROOT = Path.cwd()
KIT_ROOT = Path(__file__).resolve().parents[1]
SUBMODULE_HINT = (
    "    git submodule add https://github.com/voltygg/cs2-kit.git vendor/cs2-kit\n"
    "    git submodule update --init --recursive"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Initialize a plugin project around a vendored cs2-kit."
    )
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
    parser.add_argument(
        "--conan",
        action="store_true",
        help="consume cs2-kit as a Conan package instead of the vendored submodule "
        "(see docs/consuming-via-conan.md; no submodules needed in the new repo)",
    )
    args = parser.parse_args()

    if not args.conan and (REPO_ROOT / "vendor/cs2-kit").resolve() != KIT_ROOT:
        print(
            "error: cs2-kit must be vendored at vendor/cs2-kit under the current\n"
            "directory. From your project root, run:\n" + SUBMODULE_HINT
        )
        return 1

    template_dir = KIT_ROOT / "templates" / ("project-conan" if args.conan else "project")

    for existing in ("CMakeLists.txt", "conanfile.py"):
        if (REPO_ROOT / existing).exists():
            print(f"error: {existing} already exists in {REPO_ROOT}; refusing to overwrite.")
            return 1

    plugin_dir = REPO_ROOT / "plugins" / args.plugin
    if plugin_dir.exists():
        print(f"error: {plugin_dir} already exists; refusing to overwrite.")
        return 1

    if not template_dir.is_dir():
        print(f"error: template tree missing at {template_dir}.")
        return 1

    if not args.conan:
        sdk_dir = REPO_ROOT / "vendor/cs2-kit/vendor/hl2sdk-cs2"
        if not sdk_dir.is_dir() or not any(sdk_dir.iterdir()):
            print(
                "note: the kit's SDK submodules are not initialized yet; the build (or\n"
                "`uv run poe bootstrap`) needs: git submodule update --init --recursive"
            )

    new_plugin.render_tree(template_dir, REPO_ROOT, {"project": args.name}, safe=True)
    if (code := new_plugin.scaffold_plugin(args.plugin)) != 0:
        return code

    if args.conan:
        print("\nDone. See README.md for the remote/profile setup and build commands.")
    else:
        print(
            "\nDone. Next steps:\n"
            "  uv sync           # provision CMake/Conan/Ninja (https://docs.astral.sh/uv)\n"
            "  uv run poe build  # conan install + cmake workflow preset\n"
            "Without uv: python vendor/cs2-kit/scripts/bootstrap.py"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
