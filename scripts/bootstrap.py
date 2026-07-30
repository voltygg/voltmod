#!/usr/bin/env python3
"""One-command setup: fetch submodules, check build tools, then build.

Targets the working directory: python vendor/cs2-kit/scripts/bootstrap.py
"""

import subprocess
from pathlib import Path

import buildtools

ROOT = Path.cwd()


def main() -> None:
    """Fetch submodules (cs2-kit + nested SDKs), then build (which verifies tools)."""
    print("==> [1/2] Fetching submodules (cs2-kit + nested SDKs)")
    subprocess.run(
        ["git", "submodule", "update", "--init", "--recursive", "--depth", "1"],
        cwd=ROOT,
        check=True,
    )

    print("==> [2/2] Building with Conan + CMake")
    buildtools.build(ROOT, buildtools.default_preset())

    print(
        "\n============================================================\n"
        "  Bootstrap complete - the plugins built successfully.\n"
        "  Output: build/<preset>/plugins/\n"
        "============================================================"
    )


if __name__ == "__main__":
    main()
