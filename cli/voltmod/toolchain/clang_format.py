import tempfile
from itertools import batched
from pathlib import Path

from voltmod.files import write_if_changed
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


def format_cpp_texts(repo: Path, files: dict[Path, str]) -> dict[Path, str]:
    """`files` with the C++ ones formatted by one clang-format run."""
    scratch_parent = repo / "build"
    scratch_parent.mkdir(exist_ok=True)
    # Inside the repo, so clang-format finds the .clang-format the real paths would.
    with tempfile.TemporaryDirectory(dir=scratch_parent) as scratch:
        staged = {
            relative: Path(scratch) / relative
            for relative in files
            if relative.suffix in CPP_SUFFIXES
        }
        for relative, path in staged.items():
            write_if_changed(path, files[relative])
        format_cpp_files(list(staged.values()))
        # Bytes, so Windows newline translation cannot change what clang-format wrote.
        formatted = {relative: path.read_bytes().decode() for relative, path in staged.items()}
    return files | formatted
