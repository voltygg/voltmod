"""The `voltmod database` commands: render a migration, and generate its sqlpp23 table specs."""

import difflib
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Annotated

import typer

from voltmod.tools import abort, framework_root

from .dialect import DRIVERS, resolve

app = typer.Typer(help="Migration rendering and table-spec generation.")

TEMPLATE = framework_root() / "templates/database/table-header.in"


def _migration_files(directory: Path) -> list[Path]:
    """Every `NNNN_*.sql` in `directory`, in version order."""
    numbered = []
    for path in sorted(directory.glob("*.sql")):
        match = re.match(r"(\d+)", path.name)
        if match:
            numbered.append((int(match.group(1)), path))
    if not numbered:
        abort(f"no NNNN_*.sql migrations in {directory}")
    return [path for _, path in sorted(numbered)]


def render(source: Path, driver: str) -> str:
    """One SQL file, or a whole migration directory in version order, rendered for `driver`."""
    files = [source] if source.is_file() else _migration_files(source)
    return "\n".join(resolve(path.read_text(encoding="utf-8"), driver) for path in files)


def _ddl2cpp(root: Path) -> Path:
    """conan's generated data file is the only handle on the script sqlpp23 ships."""
    for data in sorted((root / "build").glob("*/generators/Sqlpp23-*-data.cmake")):
        match = re.search(
            r'set\(sqlpp23_PACKAGE_FOLDER_\w+ "([^"]+)"\)', data.read_text(encoding="utf-8")
        )
        if match:
            script = Path(match.group(1)) / "bin" / "sqlpp23-ddl2cpp"
            if script.is_file():
                return script
    abort("sqlpp23-ddl2cpp not found; run `voltmod build` once so conan resolves the package")


def generate(root: Path, ddl: str, namespace: str, header_name: str) -> str:
    """Run sqlpp23-ddl2cpp over `ddl`; its input and output stay out of the tree."""
    with tempfile.TemporaryDirectory() as work:
        source = Path(work) / "schema.sql"
        source.write_text(ddl, encoding="utf-8", newline="\n")
        target = Path(work) / header_name
        subprocess.run(
            [
                sys.executable,
                str(_ddl2cpp(root)),
                "--path-to-ddl", str(source),
                "--path-to-header", str(target),
                "--namespace", namespace,
                "--naming-style", "camel-case",
                "--assume-auto-id",
                "--path-to-custom-template", str(TEMPLATE),
            ],
            check=True,
            cwd=root,
        )
        return target.read_text(encoding="utf-8")


@app.command()
def tables(
    migrations: Annotated[
        str, typer.Option("--migrations", help="Directory holding the NNNN_*.sql migrations.")
    ],
    header: Annotated[
        str, typer.Option("--header", help="Table-spec header to write.")
    ],
    namespace: Annotated[
        str, typer.Option("--namespace", help="C++ namespace for the generated specs.")
    ],
    check: Annotated[
        bool, typer.Option("--check", help="Fail instead of writing when out of date.")
    ] = False,
    root: Annotated[str, typer.Option("--root", help="Repository root.")] = ".",
) -> None:
    """Generate the sqlpp23 table specs a plugin's migrations describe.

    The Postgres rendering is the one the DDL parser reads; it skips the indexes and seed rows
    it cannot parse, so the migrations stay the single source of truth.
    """
    repo = Path(root).resolve()
    target = Path(header)
    ddl = render(Path(migrations), "postgres")
    generated = generate(repo, ddl, namespace, target.name)

    current = target.read_text(encoding="utf-8") if target.is_file() else ""
    if not check:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(generated, encoding="utf-8", newline="\n")
        print(f"Generated {target}.")
        return

    if current != generated:
        sys.stdout.writelines(
            difflib.unified_diff(
                current.splitlines(True), generated.splitlines(True), "committed", "generated"
            )
        )
        abort(f"{target} is out of date; run the generator without --check")
    print(f"{target} is up to date.")


@app.command()
def sql(
    source: Annotated[
        str, typer.Argument(help="A .sql file, or a directory of NNNN_*.sql migrations.")
    ],
    driver: Annotated[
        str, typer.Option("--driver", help=f"One of {', '.join(DRIVERS)}.")
    ] = "postgres",
) -> None:
    """Print SQL as the server would apply it on `driver`, ready to pipe into a client."""
    if driver not in DRIVERS:
        abort(f"unknown driver '{driver}'; expected {', '.join(DRIVERS)}")
    print(render(Path(source), driver))
