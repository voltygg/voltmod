"""Unified build, scaffold, and package CLI for VoltMod projects."""

import argparse
from pathlib import Path

from . import buildtools, init_project, modgraph, new_plugin, package

ROOT = Path.cwd()
KIT_ROOT = Path(__file__).resolve().parents[2]
CONFIG_SOURCE = "https://github.com/voltygg/voltmod.git"


def _build(args: argparse.Namespace) -> int:
    options: list[str] = []
    for option in args.option:
        options += ["-o", option]
    preset = args.preset or buildtools.default_preset()
    buildtools.build(ROOT, preset, run_tests=not args.no_test, options=options)
    return 0


def _bootstrap(args: argparse.Namespace) -> int:
    print("==> [1/2] Installing Conan profiles and remotes")
    if (ROOT / "conan/profiles").is_dir():
        source = [str(ROOT / "conan")]
    else:
        source = [CONFIG_SOURCE, "-sf", "conan"]
    buildtools.run_tool("conan", "config", "install", *source)

    print("==> [2/2] Building with Conan + CMake")
    buildtools.build(ROOT, buildtools.default_preset())

    print("\nBootstrap complete: build/<preset>/plugins/")
    return 0


def _format(args: argparse.Namespace) -> int:
    dirs = args.dirs or (["src", "include", "tests"] if ROOT == KIT_ROOT else ["plugins"])
    buildtools.format_sources(ROOT, dirs, check=args.check)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="voltmod",
        description="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("build", help="Conan install + CMake build for one preset")
    p.add_argument("preset", nargs="?", help="CMake preset (default: release for this OS)")
    p.add_argument(
        "--no-test",
        action="store_true",
        help="skip ctest, so CI can time and report tests separately",
    )
    p.add_argument(
        "-o",
        "--option",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="passed through to `conan install` (repeatable)",
    )
    p.set_defaults(run=_build)

    p = sub.add_parser("bootstrap", help="install Conan profiles and the remote, then build")
    p.set_defaults(run=_bootstrap)

    p = sub.add_parser("format", help="run the pinned clang-format over C++ sources")
    p.add_argument("--check", action="store_true", help="report diffs instead of writing them")
    p.add_argument("dirs", nargs="*", help="directories to format (default: this repo's sources)")
    p.set_defaults(run=_format)

    p = sub.add_parser("modgraph", help="check voltmod's module layering")
    p.add_argument(
        "root", nargs="?", default=".", help="repo root (default: the working directory)"
    )
    p.set_defaults(run=lambda a: modgraph.check(Path(a.root)))

    p = sub.add_parser("new-plugin", help="stamp a plugin skeleton into plugins/<name>/")
    p.add_argument("name", type=new_plugin.kebab_case, help="kebab-case name, e.g. fun-votes")
    p.set_defaults(run=lambda a: new_plugin.create(a.name))

    p = sub.add_parser("init", help="stamp a whole consumer project into the working directory")
    p.add_argument(
        "--name",
        type=new_plugin.kebab_case,
        default=ROOT.name,
        help="kebab-case project name (default: the directory name)",
    )
    p.add_argument(
        "--plugin",
        type=new_plugin.kebab_case,
        default="my-plugin",
        help="kebab-case name for the first plugin (default: my-plugin)",
    )
    p.set_defaults(run=lambda a: init_project.create(a.name, a.plugin))

    package.add_parser(sub)

    return parser


def main() -> int:
    args = build_parser().parse_args()
    return args.run(args) or 0
