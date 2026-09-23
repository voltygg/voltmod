from itertools import batched
from pathlib import Path

from voltmod.toolchain.process import run_tool

CPP_SUFFIXES = (".cpp", ".hpp", ".inc")

# Keeps a batch of full paths well under Windows' 32767-character command-line limit.
FILES_PER_RUN = 100


def find_cpp_sources(root: Path, dirs: list[str]) -> list[Path]:
    # fmt: off
    return sorted(
        path
        for name in dirs if (root / name).is_dir()
        for path in (root / name).rglob("*") if path.suffix in CPP_SUFFIXES
    )
    # fmt: on


def format_cpp_files(files: list[Path]) -> None:
    for batch in batched(files, FILES_PER_RUN):
        run_tool("clang-format", "-i", *batch)
