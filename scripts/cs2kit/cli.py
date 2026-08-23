"""The `cs2kit` command: one entry point for the whole toolchain.

Every subcommand targets the working directory, so the same binary serves cs2-kit
itself and any repo that depends on it:

    cs2kit build [preset] [--no-test] [-o name=value ...]
    cs2kit bootstrap
    cs2kit format [--check] [dirs ...]
    cs2kit modgraph [root]
    cs2kit new-plugin <name>
    cs2kit init [--name <n>] [--plugin <p>]
    cs2kit package <build|publish|prune|watch>

This replaced six separate console scripts. They shared argument parsing, a
working-directory convention and error style, but each reimplemented them by hand
from sys.argv; collecting them here is what lets `cs2kit package` (which needs
subcommands of its own) exist without a seventh.
"""

import argparse
import subprocess
from pathlib import Path

from . import buildtools, init_project, modgraph, new_plugin, package

ROOT = Path.cwd()
# The kit checkout, when running from one rather than from an installed wheel.
KIT_ROOT = Path(__file__).resolve().parents[2]
CONFIG_SOURCE = "https://github.com/voltygg/cs2-kit.git"


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
        # This is cs2-kit itself: its own conan/ dir is the canonical copy.
        source = [str(ROOT / "conan")]
    else:
        source = [CONFIG_SOURCE, "-sf", "conan"]
    argv, env = buildtools.resolve_tool("conan")
    subprocess.run([*argv, "config", "install", *source], check=True, env=env)

    print("==> [2/2] Building with Conan + CMake")
    buildtools.build(ROOT, buildtools.default_preset())

    print(
        "\n============================================================\n"
        "  Bootstrap complete - the plugins built successfully.\n"
        "  Output: build/<preset>/plugins/\n"
        "============================================================"
    )
    return 0


def _format(args: argparse.Namespace) -> int:
    # The kit's own sources when run from the kit root, else the consumer layout.
    dirs = args.dirs or (["src", "include", "tests"] if ROOT == KIT_ROOT else ["plugins"])
    buildtools.format_sources(ROOT, dirs, check=args.check)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="cs2kit",
        description="Build and scaffolding tooling for CS2 Metamod:Source plugin projects.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("build", help="Conan install + CMake build for one preset")
    p.add_argument("preset", nargs="?", help="CMake preset (default: release for this OS)")
    p.add_argument("--no-test", action="store_true",
                   help="skip ctest, so CI can time and report tests separately")
    p.add_argument("-o", "--option", action="append", default=[], metavar="NAME=VALUE",
                   help="passed through to `conan install` (repeatable)")
    p.set_defaults(run=_build)

    p = sub.add_parser("bootstrap", help="install Conan profiles and the remote, then build")
    p.set_defaults(run=_bootstrap)

    p = sub.add_parser("format", help="run the pinned clang-format over C++ sources")
    p.add_argument("--check", action="store_true", help="report diffs instead of writing them")
    p.add_argument("dirs", nargs="*", help="directories to format (default: this repo's sources)")
    p.set_defaults(run=_format)

    p = sub.add_parser("modgraph", help="check cs2-kit's module layering")
    p.add_argument("root", nargs="?", default=".",
                   help="repo root (default: the working directory)")
    p.set_defaults(run=lambda a: modgraph.check(Path(a.root)))

    p = sub.add_parser("new-plugin", help="stamp a plugin skeleton into plugins/<name>/")
    p.add_argument("name", type=new_plugin.kebab_case, help="kebab-case name, e.g. fun-votes")
    p.set_defaults(run=lambda a: new_plugin.create(a.name))

    p = sub.add_parser("init", help="stamp a whole consumer project into the working directory")
    p.add_argument("--name", type=new_plugin.kebab_case, default=ROOT.name,
                   help="kebab-case project name (default: the directory name)")
    p.add_argument("--plugin", type=new_plugin.kebab_case, default="my-plugin",
                   help="kebab-case name for the first plugin (default: my-plugin)")
    p.set_defaults(run=lambda a: init_project.create(a.name, a.plugin))

    package.add_parser(sub)

    return parser


def main() -> int:
    args = build_parser().parse_args()
    return args.run(args) or 0
