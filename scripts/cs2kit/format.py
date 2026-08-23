#!/usr/bin/env python3
"""Format C++ sources with clang-format. Usage: cs2kit-format [--check] [dirs...]

Targets the working directory. Defaults to the kit's own sources when run from the
kit root, else plugins/ (the consumer layout); pass explicit dirs to override.
"""

import sys
from pathlib import Path

from . import buildtools

ROOT = Path.cwd()
# The kit checkout, when this is running from one rather than from a wheel.
KIT_ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    args = sys.argv[1:]
    check = "--check" in args
    dirs = [a for a in args if a != "--check"]
    if not dirs:
        dirs = ["src", "include", "tests"] if ROOT == KIT_ROOT else ["plugins"]
    buildtools.format_sources(ROOT, dirs, check=check)


