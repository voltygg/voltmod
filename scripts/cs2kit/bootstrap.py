#!/usr/bin/env python3
"""One-command setup: install Conan profiles and the remote, then build.

Targets the working directory: cs2kit-bootstrap
"""

import subprocess
from pathlib import Path

from . import buildtools

ROOT = Path.cwd()
CONFIG_SOURCE = "https://github.com/voltygg/cs2-kit.git"


def main() -> None:
    """Install the shared Conan config, then build (which verifies the toolchain)."""
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


