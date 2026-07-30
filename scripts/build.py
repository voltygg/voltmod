#!/usr/bin/env python3
"""Build the current repo with Conan + CMake. Usage: build.py [preset] [--no-test]

Targets the working directory, so it serves cs2-kit itself and any repo that
vendors it: python vendor/cs2-kit/scripts/build.py [preset]
--no-test skips the ctest step so CI can run tests separately.
"""

import sys
from pathlib import Path

import buildtools

ROOT = Path.cwd()


def main() -> None:
    """Build the requested preset (default: release for this OS)."""
    args = sys.argv[1:]
    run_tests = "--no-test" not in args
    args = [a for a in args if a != "--no-test"]
    preset = args[0] if args else buildtools.default_preset()
    buildtools.build(ROOT, preset, run_tests=run_tests)


if __name__ == "__main__":
    main()
