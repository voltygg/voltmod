"""Finding C++ sources and formatting them with the pinned clang-format."""

from pathlib import Path

from voltmod.toolchain.process import run_tool

CPP_SUFFIXES = (".cpp", ".hpp", ".inc")

# Stay well under Windows' 32767-character command-line limit.
MAX_COMMAND_LINE = 24000


def find_cpp_sources(root: Path, dirs: list[str]) -> list[Path]:
    # fmt: off
    return sorted(
        path
        for name in dirs if (root / name).is_dir()
        for path in (root / name).rglob("*") if path.suffix in CPP_SUFFIXES
    )
    # fmt: on


def format_cpp_files(files: list[Path]) -> None:
    """Run clang-format in place, in batches that fit on a Windows command line."""
    batch: list[str] = []
    length = 0
    for file in map(str, files):
        if batch and length + len(file) + 3 > MAX_COMMAND_LINE:
            run_tool("clang-format", "-i", *batch)
            batch, length = [], 0
        batch.append(file)
        length += len(file) + 3  # quotes and a separator
    if batch:
        run_tool("clang-format", "-i", *batch)
