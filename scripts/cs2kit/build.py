#!/usr/bin/env python3
"""Build the current repo with Conan + CMake.

Usage: cs2kit-build [preset] [--no-test] [-o name=value ...]

Targets the working directory, so it serves cs2-kit itself and any repo that
depends on it. --no-test skips the ctest step so CI can time tests separately.
-o is passed straight through to `conan install`, which is how you build the kit
with the same options a consumer resolves it with (`-o cs2-kit/*:with_postgres=True`).
"""

import sys
from pathlib import Path

from . import buildtools

ROOT = Path.cwd()


def main() -> None:
    """Build the requested preset (default: release for this OS)."""
    args = sys.argv[1:]
    run_tests = "--no-test" not in args
    args = [a for a in args if a != "--no-test"]

    options, rest = [], []
    it = iter(args)
    for arg in it:
        if arg == "-o":
            options += ["-o", next(it, "")]
        elif arg.startswith("-o"):
            options += ["-o", arg[2:]]
        else:
            rest.append(arg)

    preset = rest[0] if rest else buildtools.default_preset()
    buildtools.build(ROOT, preset, run_tests=run_tests, options=options)


